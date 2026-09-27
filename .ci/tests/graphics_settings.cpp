#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
#define LOG_INFO(...)
#define LOG_INFO_ONCE(...)
namespace vk {
enum class PresentModeKHR { eFifo,
    eImmediate,
    eMailbox,
    eFifoRelaxed };
using DeviceSize = uint64_t;
using ComponentMapping = int;
struct AccessFlagBits {
    static constexpr int eColorAttachmentWrite = 1, eShaderWrite = 2, eTransferWrite = 4, eTransferRead = 8;
};
struct PipelineStageFlagBits {
    static constexpr int eColorAttachmentOutput = 1, eFragmentShader = 2, eTransfer = 4;
};
struct BufferUsageFlagBits {
    static constexpr int eTransferDst = 1, eTransferSrc = 2;
};
struct MemoryBarrier {
    int srcAccessMask, dstAccessMask;
};
struct ImageLayout {
    static constexpr int eGeneral = 0, eTransferDstOptimal = 1;
};
struct Offset {
    int32_t x, y, z;
};
struct Extent {
    uint32_t width, height, depth;
};
struct ImageCopy {
    int srcSubresource;
    Offset srcOffset;
    int dstSubresource;
    Offset dstOffset;
    Extent extent;
};
struct BufferImageCopy {
    uint64_t bufferOffset;
    uint32_t bufferRowLength, bufferImageHeight;
    int imageSubresource;
    Offset imageOffset;
    Extent imageExtent;
    auto &setBufferOffset(uint64_t v) {
        bufferOffset = v;
        return *this;
    }
    auto &setBufferRowLength(uint32_t v) {
        bufferRowLength = v;
        return *this;
    }
    auto &setImageOffset(Offset v) {
        imageOffset = v;
        return *this;
    }
    auto &setImageExtent(Extent v) {
        imageExtent = v;
        return *this;
    }
};
struct ClearColorValue {
    std::array<float, 4> value;
};
struct CommandBuffer {
    bool source_ready = false, cleared = false, clear_ordered = false, buffer_written = false, buffer_visible = false;
    int copies = 0;
    Extent extent{};
    void pipelineBarrier(int src, int dst, int, MemoryBarrier b, int, int) {
        assert(dst == PipelineStageFlagBits::eTransfer);
        if (b.srcAccessMask & AccessFlagBits::eColorAttachmentWrite) {
            assert((src & PipelineStageFlagBits::eFragmentShader) && (b.srcAccessMask & AccessFlagBits::eShaderWrite));
            assert(b.dstAccessMask == AccessFlagBits::eTransferRead);
            source_ready = true;
        } else {
            assert(src == PipelineStageFlagBits::eTransfer && b.srcAccessMask == AccessFlagBits::eTransferWrite);
            if (b.dstAccessMask == AccessFlagBits::eTransferWrite) {
                assert(cleared);
                clear_ordered = true;
            }
            if (b.dstAccessMask == AccessFlagBits::eTransferRead) {
                assert(buffer_written);
                buffer_visible = true;
            }
        }
    }
    void clearColorImage(int, int, ClearColorValue, int) { cleared = true; }
    void copyImage(int, int, int, int, ImageCopy c) {
        assert(source_ready && (!cleared || clear_ordered));
        extent = c.extent;
        ++copies;
    }
    void copyImageToBuffer(int, int, int, BufferImageCopy) {
        assert(source_ready);
        buffer_written = true;
    }
    void copyBufferToImage(int, int, int, BufferImageCopy c) {
        assert(buffer_visible);
        extent = c.imageExtent;
        ++copies;
    }
};
} // namespace vk
namespace vkutil {
constexpr int color_subresource_range = 0, color_subresource_layer = 0;
struct Buffer {
    uint64_t size = 0;
    int buffer = 0;
    Buffer() = default;
    explicit Buffer(uint64_t s)
        : size(s) {}
    void init_buffer(int) { buffer = 1; }
};
} // namespace vkutil
namespace renderer {
struct State {
    std::atomic<bool> vsync_enabled{ true };
};
} // namespace renderer
struct Device {
    std::vector<vk::PresentModeKHR> modes;
    auto getSurfacePresentModesKHR(int) { return modes; }
};
struct State : renderer::State {
    Device physical_device;
};
struct ScreenRenderer {
    State state;
    int surface = 0;
    vk::PresentModeKHR present_mode{};
    void select_present_mode();
};
// INSERT_PRESENT
constexpr int SCE_GXM_TEXTURE_LINEAR_STRIDED = 1;
struct SceGxmTexture {
    int type = 0;
    uint32_t vaddr_mode = 0, uaddr_mode = 0, mip_filter = 0, min_filter = 0, mag_filter = 0, lod_bias = 0, lod_min0 = 0, lod_min1 = 0;
    int texture_type() const { return type; }
};
struct SamplerCacheInfo {
    uint32_t value = 0;
    int index = 0;
};
struct TextureCache {
    bool support_depth_linear_filtering = true;
    int anisotropic_filtering = 1, last_bound_sampler_index = 0, creations = 0;
    struct Queue {
        SamplerCacheInfo entry;
        auto get_lru() { return &entry; }
        void set_as_mru(SamplerCacheInfo *) {}
    } sampler_queue;
    std::map<uint32_t, SamplerCacheInfo *> sampler_lookup;
    void configure_sampler(int, const SceGxmTexture &, bool) { ++creations; }
    int cache_and_bind_sampler(const SceGxmTexture &, bool);
};
// INSERT_SAMPLER
struct Image {
    int view = 1, layout = 0, format = 1, image = 1;
};
namespace vkutil {
using Image = ::Image;
}
using SceGxmColorBaseFormat = int;
// INSERT_CAST_TYPE
struct TextureLookupResult {
    int view, layout, format;
};
static std::optional<TextureLookupResult> lookup(std::vector<CastedTexture> &casted_vec, int vk_format, int resulting_swizzle) {
    uint32_t width = 8, height = 8, start_sourced_line = 0, start_x = 0;
    int base_format = 1;
    uint64_t scene_timestamp = 7;
    // INSERT_CAST_LOOKUP
    return std::nullopt;
}
struct DestroyQueue {
    void add_buffer(vkutil::Buffer &) {}
};
struct Frame {
    DestroyQueue destroy_queue;
};
struct CopyState {
    float res_multiplier = 1;
    Frame f;
    Frame &frame() { return f; }
};
struct Info {
    uint32_t width = 8, height = 8, stride_bytes = 32;
    Image texture;
};
static uint32_t align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
static vk::CommandBuffer copy(bool partial_surface, uint32_t bytes_per_pixel_requested) {
    CopyState state;
    Info info;
    CastedTexture storage;
    CastedTexture *casted = &storage;
    vk::CommandBuffer cmd_buffer;
    uint64_t scene_timestamp = 8;
    uint32_t width = partial_surface ? 16 : 8, height = partial_surface ? 16 : 8, start_x = 0, start_sourced_line = 0;
    uint32_t stride_bytes = 32, bytes_per_pixel_in_store = 4;
    // INSERT_COPY
    return cmd_buffer;
}
struct Counter {
    // INSERT_COUNTER
};
int main() {
    Counter metrics;
    ++metrics.frame_count;
    const size_t snapshot = metrics.frame_count.load(std::memory_order_relaxed);
    assert(snapshot == 1 && metrics.frame_count.exchange(0, std::memory_order_relaxed) == 1);
    assert(metrics.frame_count.load(std::memory_order_relaxed) == 0);
    ScreenRenderer s;
    using Mode = vk::PresentModeKHR;
    for (bool sync : { false, true }) {
        s.state.vsync_enabled = sync;
        s.state.physical_device.modes = { Mode::eFifo };
        s.select_present_mode();
        assert(s.present_mode == Mode::eFifo);
    }
    s.state.vsync_enabled = false;
    s.state.physical_device.modes = { Mode::eFifo, Mode::eImmediate };
    s.select_present_mode();
    assert(s.present_mode == Mode::eImmediate);
    s.state.physical_device.modes.push_back(Mode::eMailbox);
    s.select_present_mode();
    assert(s.present_mode == Mode::eMailbox);
    s.state.vsync_enabled = true;
    s.select_present_mode();
    assert(s.present_mode == Mode::eFifo);
    TextureCache cache;
    SceGxmTexture texture;
    cache.cache_and_bind_sampler(texture, false);
    assert(cache.creations == 1);
    cache.cache_and_bind_sampler(texture, false);
    assert(cache.creations == 1);
    cache.anisotropic_filtering = 16;
    cache.cache_and_bind_sampler(texture, false);
    assert(cache.creations == 2);
    cache.anisotropic_filtering = 1;
    cache.cache_and_bind_sampler(texture, false);
    assert(cache.creations == 3);
    texture.type = SCE_GXM_TEXTURE_LINEAR_STRIDED;
    cache.cache_and_bind_sampler(texture, false);
    assert(cache.creations == 4);
    std::vector<CastedTexture> casted(1);
    casted[0].cropped_width = casted[0].cropped_height = 8;
    casted[0].scene_timestamp = 7;
    casted[0].format = 1;
    casted[0].components = 1;
    assert(lookup(casted, 1, 1));
    assert(!lookup(casted, 2, 1)); // Same Vita format, different host gamma format.
    assert(!lookup(casted, 1, 2)); // Same pixels, different channel mapping.
    casted[0].scene_timestamp = 6;
    assert(!lookup(casted, 1, 1));
    auto partial = copy(true, 4);
    assert(partial.copies == 1 && partial.clear_ordered && partial.extent.width == 8 && partial.extent.height == 8);
    auto full = copy(false, 4);
    assert(full.copies == 1 && !full.cleared);
    auto typeless = copy(false, 2);
    assert(typeless.copies == 1 && typeless.buffer_visible);
}
