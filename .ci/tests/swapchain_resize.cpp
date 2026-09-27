#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#define LOG_INFO(...) ((void)0)
namespace vk {
struct Extent2D {
    uint32_t width = 0, height = 0;
};
} // namespace vk
struct Frame {
    int width = 1920, height = 1080;
    int drawable_width() const { return width; }
    int drawable_height() const { return height; }
};
namespace renderer {
struct State {
    Frame *frame;
};
} // namespace renderer
struct Capabilities {
    vk::Extent2D currentExtent{ 2208, 1242 };
    vk::Extent2D minImageExtent{ 1, 1 }, maxImageExtent{ 4096, 4096 };
};
struct VKState : renderer::State {
    struct {
        Capabilities caps;
        auto getSurfaceCapabilitiesKHR(int) { return caps; }
    } physical_device;
    struct {
        int idle_calls = 0;
        void waitIdle() { ++idle_calls; }
    } device;
};
// INSERT_VISIBLE
struct ScreenRenderer {
    VKState &state;
    int surface = 1;
    bool need_rebuild = false, need_surface_recreate = false, swapchain = false;
    vk::Extent2D extent{}, window_extent{};
    Capabilities surface_capabilities{};
    int builds = 0, surface_builds = 0;
    bool surface_matches_window_size();
    bool ensure_swapchain();
    bool rebuild_swapchain_if_visible();
    void destroy_swapchain() { swapchain = false; }
    bool create() {
        ++surface_builds;
        need_surface_recreate = false;
        return true;
    }
    void create_swapchain() {
        // INSERT_EXTENT
        swapchain = true;
        ++builds;
    }
};
// INSERT_REBUILD
int main() {
    Frame frame;
    VKState state;
    state.frame = &frame;
    ScreenRenderer screen{ state };
    assert(screen.ensure_swapchain());
    assert(screen.extent.width == 2208 && screen.window_extent.width == 1920);
    for (int i = 0; i < 100; ++i)
        assert(screen.ensure_swapchain());
    assert(screen.builds == 1 && state.device.idle_calls == 1);
    // Rotation changes the window dimensions and triggers exactly one rebuild.
    frame.width = 1080;
    frame.height = 1920;
    state.physical_device.caps.currentExtent = { 1242, 2208 };
    assert(screen.ensure_swapchain());
    assert(screen.builds == 2 && screen.extent.height == 2208);
    assert(screen.ensure_swapchain() && screen.builds == 2);
    // Explicit vsync/out-of-date and surface-lost rebuild requests still work.
    screen.need_rebuild = true;
    assert(screen.ensure_swapchain() && screen.builds == 3);
    screen.need_surface_recreate = true;
    assert(screen.ensure_swapchain() && screen.surface_builds == 1);
    frame.width = frame.height = 0;
    assert(!screen.ensure_swapchain() && screen.builds == 4);
    // Variable-size surfaces can clamp the requested dimensions, too.
    state.physical_device.caps.currentExtent.width = std::numeric_limits<uint32_t>::max();
    state.physical_device.caps.maxImageExtent = { 1024, 1024 };
    frame.width = 1920;
    frame.height = 1080;
    assert(screen.ensure_swapchain() && screen.builds == 5);
    assert(screen.extent.width == 1024 && screen.extent.height == 1024);
    for (int i = 0; i < 100; ++i)
        assert(screen.ensure_swapchain());
    assert(screen.builds == 5 && state.device.idle_calls == 5);
}
