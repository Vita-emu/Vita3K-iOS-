#include <util/ios_runtime_tuning.h>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <future>
#include <thread>
#include <memory>
#include <limits>
#include <set>
#include <vector>

static int freed_contexts = 0, premature_frees = 0;
static void sws_freeContext(int *context) { if (context) { ++freed_contexts; delete context; } }
namespace vkutil {
struct Image {
    void *image = nullptr;
    void *view = nullptr;
    ~Image() { if (image) ++premature_frees; }
};
struct Buffer {
    void *buffer = nullptr;
    uint64_t size = 0;
    ~Buffer() { if (buffer) ++premature_frees; }
};
struct DestroyQueue {
    std::set<void *> handles;
    void add(void *handle) { if (handle) assert(handles.insert(handle).second); }
    void add_image(Image &image) { add(image.image); add(image.view); image.image = image.view = nullptr; }
    void add_buffer(Buffer &buffer) { add(buffer.buffer); buffer.buffer = nullptr; }
};
}
struct Casted { vkutil::Buffer transition_buffer; vkutil::Image texture; };
struct View { void *view; };
struct ColorSurfaceCacheInfo {
    std::vector<Casted> casted_textures;
    std::vector<View> sampled_views;
    void *alternate_view = nullptr;
    std::unique_ptr<vkutil::Image> blit_image;
    std::unique_ptr<vkutil::Buffer> copy_buffer;
    int *sws_context = nullptr;
    bool need_post_surface_sync = true, need_buffer_sync = true;
    vkutil::Image texture;
    unsigned stride_bytes = 0, original_height = 0;
    bool format = false;
};
static int sync_waits = 0;
struct DepthSurfaceView { vkutil::Image stencil_view, depth_view; };
struct DepthStencilSurfaceCacheInfo {
    std::vector<DepthSurfaceView> read_surfaces;
    void *depth_view = nullptr, *stencil_view = nullptr;
    vkutil::Image texture;
};
struct Frame { vkutil::DestroyQueue destroy_queue; };
struct SurfaceReadbackBarrierRequest { std::shared_ptr<std::promise<void>> completed; };
struct RequestQueue {
    bool aborted = false;
    std::thread worker;
    ~RequestQueue() { if (worker.joinable()) worker.join(); }
    bool is_aborted() const { return aborted; }
    void push(SurfaceReadbackBarrierRequest request) {
        ++sync_waits;
        if (worker.joinable()) worker.join();
        worker = std::thread([request] { request.completed->set_value(); });
    }
};
struct State { Frame f; RequestQueue request_queue; Frame &frame() { return f; } };
using VKState = State;
// INSERT_READBACK_BARRIER
struct VKSurfaceCache {
    State state;
    std::set<void *> retired_framebuffer_views;
    void destroy_framebuffers(void *view) { if (view) retired_framebuffer_views.insert(view); }
    void destroy_surface(ColorSurfaceCacheInfo &info);
    void destroy_surface(DepthStencilSurfaceCacheInfo &info);
};
// INSERT_SURFACE_RETIREMENT
// INSERT_DEPTH_RETIREMENT
static bool format_need_additional_memory(bool format) { return format; }
static uint64_t staging_size(ColorSurfaceCacheInfo *last_written_surface) {
    vkutil::Buffer copy_buffer;
    // INSERT_READBACK_SIZE
    return copy_buffer.size;
}
static bool staging_needs_wait(uint64_t previous_frame, uint64_t frame, bool use_previous_buffer = false) {
    struct Staging { uint64_t frame_timestamp; uint64_t scene_timestamp = 1; };
    Staging staging{previous_frame};
    const auto *staging_buffer = &staging;
    struct Context { uint64_t frame_timestamp; } current{frame};
    const auto *context = &current;
    constexpr uint64_t last_waited_scene = 0;
    constexpr int MAX_FRAMES_RENDERING = 3;
    // INSERT_FENCE_CHECK
    return need_wait;
}
static void *handle(int value) { return reinterpret_cast<void *>(static_cast<uintptr_t>(value)); }
int main() {
    using namespace ios_runtime;
    for (int invalid : {-100, -1, 1, 8, 9, 11, 13, 33, 4096}) assert(jit_cache_mib(invalid) == 0);
    for (int valid : {12, 16, 24, 32}) assert(jit_cache_mib(valid) == valid);
    assert(shader_workers(0, 4) == 0);
    assert(shader_workers(4, 2) == 2);
    assert(shader_workers(2, 0) == 1);
    assert(shader_workers(-1, 6) == 0);
    assert(shader_workers(5, 6) == 0);
    assert(texture_entries(0, 3072) == 128);
    assert(texture_entries(0, 3073) == 512);
    assert(texture_entries(0, 0) == 512);
    assert(texture_entries(-100, 3072) == 128);
    for (int valid : {128, 256, 512}) assert(texture_entries(valid, 3072) == valid);
    constexpr uint64_t mib = 1024 * 1024;
    assert(shrink_staging(16 * mib, 4 * mib, 240, 120));
    assert(!shrink_staging(16 * mib, 4 * mib + 1, 240, 120));
    assert(!shrink_staging(16 * mib, mib, 239, 120));
    assert(!shrink_staging(16 * mib, mib, 10, 120));
    assert(!shrink_staging(4 * mib, mib, 240, 120));
    assert(staging_needs_wait(0, 0));
    assert(staging_needs_wait(0, 1));
    assert(staging_needs_wait(0, 2));
    assert(!staging_needs_wait(0, 3));
    assert(!staging_needs_wait(1, 1, true));
    assert(!staging_needs_wait(~uint64_t(0), 0));
    VKSurfaceCache cache;
    ColorSurfaceCacheInfo info;
    info.texture.image = handle(1); info.texture.view = handle(2);
    info.alternate_view = handle(3);
    info.blit_image = std::make_unique<vkutil::Image>();
    info.blit_image->image = handle(4); info.blit_image->view = handle(5);
    info.copy_buffer = std::make_unique<vkutil::Buffer>(); info.copy_buffer->buffer = handle(6);
    info.sws_context = new int(1);
    info.sampled_views.push_back({handle(7)});
    cache.destroy_surface(info);
    assert(cache.state.f.destroy_queue.handles.size() == 7);
    assert(cache.retired_framebuffer_views == std::set<void *>({handle(2), handle(3)}));
    assert(!info.alternate_view && !info.blit_image && !info.copy_buffer && !info.sws_context);
    assert(!info.need_post_surface_sync && !info.need_buffer_sync);
    assert(freed_contexts == 1 && premature_frees == 0 && sync_waits == 1);
    // Repeated invalidation and reuse must neither double-free nor retain an
    // allocation sized for the old owner of this LRU slot.
    cache.destroy_surface(info);
    assert(freed_contexts == 1 && cache.state.f.destroy_queue.handles.size() == 7);
    info.texture.image = handle(8); info.texture.view = handle(9);
    cache.destroy_surface(info);
    assert(cache.state.f.destroy_queue.handles.size() == 9);
    DepthStencilSurfaceCacheInfo depth;
    depth.texture.image = handle(10); depth.texture.view = handle(11);
    depth.depth_view = handle(12); depth.stencil_view = handle(13);
    cache.destroy_surface(depth);
    cache.destroy_surface(depth);
    assert(!depth.depth_view && !depth.stencil_view);
    assert(cache.state.f.destroy_queue.handles.size() == 13);
    cache.state.request_queue.aborted = true;
    wait_for_surface_readbacks(cache.state);
    assert(sync_waits == 1);
    info.stride_bytes = 960 * 3; info.original_height = 544; info.format = true;
    assert(staging_size(&info) == 960ULL * 4 * 544);
    info.stride_bytes = 960 * 4; info.format = false;
    assert(staging_size(&info) == 960ULL * 4 * 544);
}
