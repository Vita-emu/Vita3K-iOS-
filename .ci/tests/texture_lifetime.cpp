// Production cache bind/range functions are injected by test_texture_lifetime.py.
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <mem/allocator.h>
#include <mutex>

using Address = uint32_t;
constexpr uint32_t STANDARD_PAGE_SIZE = 4096;
constexpr int SCE_GXM_TEXTURE_LINEAR_STRIDED = 1;
constexpr uint32_t SCE_GXM_TEXTURE_BASE_FORMAT_P4 = 4;
constexpr uint32_t SCE_GXM_TEXTURE_BASE_FORMAT_P8 = 8;
struct MemState {
    std::mutex generation_mutex;
    BitmapAllocator allocator{ 64 };
    uint32_t host_page_size = 16384;
};
struct SceGxmTexture {
    uint32_t data_addr = 0, palette_addr = 0, format = 0, size = STANDARD_PAGE_SIZE;
    int texture_type() const { return 0; }
};
namespace gxm {
uint32_t texture_size_first_mip(const SceGxmTexture &t) { return t.size; }
uint32_t get_format(const SceGxmTexture &t) { return t.format; }
uint32_t get_base_format(uint32_t f) { return f; }
} // namespace gxm
// INSERT_RANGE_CHECKS
using TextureGxmDataRepr = std::array<uint32_t, 4>;
struct TextureCacheInfo {
    size_t index = 0;
    SceGxmTexture texture;
    uint32_t texture_size = 0;
    uint64_t hash = 0;
    bool use_hash = true, dirty = false, is_imported = false;
};
static uint32_t align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
static uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1); }
enum class MemPerm { ReadOnly };
template <typename F>
void add_protect(MemState &, Address, uint32_t, MemPerm, F) { assert(false); }
#define R_PROFILE(...)
#define LOG_WARN_ONCE(...)
#define LOG_INFO_ONCE(...)
static int hashes = 0;
static uint64_t hash_texture_data(const SceGxmTexture &t, uint32_t size, const MemState &mem) {
    assert(texture_source_allocated(t, mem) && size == t.size);
    ++hashes;
    return 42;
}
static uint64_t hash_texture_nostride(const SceGxmTexture &t, const MemState &mem) {
    return hash_texture_data(t, t.size, mem);
}
struct TextureCache {
    bool use_sampler_cache = true, use_protect = false;
    bool import_textures = false, export_textures = false, importing_texture = false;
    int loading_texture = 0;
    struct Queue {
        TextureCacheInfo entry;
        TextureCacheInfo *get_lru() { return &entry; }
        void set_as_mru(TextureCacheInfo *) {}
    } texture_queue;
    std::map<TextureGxmDataRepr, TextureCacheInfo *> texture_lookup;
    std::map<uint64_t, int> available_textures_hash;
    TextureCacheInfo *current_info = nullptr;
    int uploads = 0, selects = 0;
    std::function<void()> on_upload;
    bool cache_and_bind_texture(const SceGxmTexture &, MemState &);
    void select(size_t, const SceGxmTexture &) { ++selects; }
    bool import_configure_texture() { return true; }
    void configure_texture(const SceGxmTexture &) {}
    void export_select(const SceGxmTexture &) {}
    void import_upload_texture() { assert(false); }
    void upload_texture(const SceGxmTexture &t, MemState &mem) {
        ++uploads;
        if (on_upload)
            on_upload();
        assert(texture_source_allocated(t, mem));
    }
    void upload_done() {}
    void export_done() {}
    void import_done() {}
    void cache_and_bind_sampler(const SceGxmTexture &) {}
};
// INSERT_CACHE_BIND
namespace vk {
struct DescriptorImageInfo {
    int sampler = 1, imageView = 1;
    DescriptorImageInfo()
        : sampler(0)
        , imageView(0) {}
};
} // namespace vk
constexpr size_t SCE_GXM_MAX_TEXTURE_UNITS = 16;
struct Context {
    struct {
        TextureCache texture_cache;
    } state;
    std::array<vk::DescriptorImageInfo, SCE_GXM_MAX_TEXTURE_UNITS> vertex_textures, fragment_textures;
    uint32_t last_vert_texture_count = 1, last_frag_texture_count = 1;
};
static void bind_released_texture(Context &context, MemState &mem, size_t index, const SceGxmTexture &texture) {
    const bool is_vertex = index >= SCE_GXM_MAX_TEXTURE_UNITS;
    // INSERT_VULKAN_FAILURE
    assert(false); // Invalid memory must take the failure path before selecting an image.
}
int main() {
    MemState mem;
    assert(mem.allocator.allocate_at(1, 3) == 0);
    SceGxmTexture movie{ STANDARD_PAGE_SIZE / 4, 0, 0, STANDARD_PAGE_SIZE * 3 };
    assert(texture_source_allocated(movie, mem));
    assert(!texture_range_allocated(mem, 0, 1));
    assert(!texture_range_allocated(mem, STANDARD_PAGE_SIZE, 0));
    assert(!texture_range_allocated(mem, UINT32_MAX - 5, 12));
    assert(!texture_range_allocated(mem, UINT64_MAX - 5, 12));
    assert(!texture_range_allocated(mem, STANDARD_PAGE_SIZE, UINT64_MAX));
    assert(texture_range_allocated(mem, STANDARD_PAGE_SIZE + 1, STANDARD_PAGE_SIZE * 3 - 1));
    mem.allocator.free(2, 1); // Middle-page hole, even though both endpoints are live.
    assert(!texture_source_allocated(movie, mem));
    TextureCache cache;
    assert(!cache.cache_and_bind_texture(movie, mem));
    assert(hashes == 0 && cache.selects == 0 && cache.uploads == 0 && cache.texture_lookup.empty());
    assert(mem.allocator.allocate_at(2, 1) == 0);
    assert(cache.cache_and_bind_texture(movie, mem));
    assert(hashes == 1 && cache.uploads == 1);
    mem.allocator.free(1, 3);
    // A cache hit must still reject a buffer freed after the previous draw.
    assert(!cache.cache_and_bind_texture(movie, mem));
    assert(hashes == 1 && cache.uploads == 1);
    assert(mem.allocator.allocate_at(1, 3) == 0);
    movie.format = SCE_GXM_TEXTURE_BASE_FORMAT_P8;
    movie.palette_addr = (4 * STANDARD_PAGE_SIZE - 64) / 64;
    assert(!texture_source_allocated(movie, mem)); // Palette extends into a free page.
    movie.format = SCE_GXM_TEXTURE_BASE_FORMAT_P4;
    assert(texture_source_allocated(movie, mem));
    movie.palette_addr = 0;
    assert(!texture_source_allocated(movie, mem));
    movie.format = 0;

    TextureCache racing_cache;
    std::promise<void> uploading, release, freeing;
    auto released = release.get_future();
    racing_cache.on_upload = [&] { uploading.set_value(); released.wait(); };
    auto bind = std::async(std::launch::async, [&] { return racing_cache.cache_and_bind_texture(movie, mem); });
    uploading.get_future().wait();
    auto destroy = std::async(std::launch::async, [&] {
        freeing.set_value();
        std::lock_guard<std::mutex> lock(mem.generation_mutex);
        mem.allocator.free(1, 3);
    });
    freeing.get_future().wait();
    assert(destroy.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    release.set_value();
    assert(bind.get());
    destroy.get();
    assert(!racing_cache.cache_and_bind_texture(movie, mem));
    assert(racing_cache.uploads == 1);
    Context context;
    context.fragment_textures[2].sampler = 7;
    context.fragment_textures[2].imageView = 9;
    context.vertex_textures[3].sampler = 11;
    bind_released_texture(context, mem, 2, movie);
    assert(context.fragment_textures[2].sampler == 0 && context.fragment_textures[2].imageView == 0);
    assert(context.last_frag_texture_count == ~uint32_t{ 0 } && context.last_vert_texture_count == 1);
    assert(context.vertex_textures[3].sampler == 11);
    bind_released_texture(context, mem, SCE_GXM_MAX_TEXTURE_UNITS + 3, movie);
    assert(context.vertex_textures[3].sampler == 0 && context.last_vert_texture_count == ~uint32_t{ 0 });
}
