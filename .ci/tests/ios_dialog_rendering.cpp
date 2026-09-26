#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

constexpr int NO_DIALOG = 0, IME_DIALOG = 1, MESSAGE_DIALOG = 2, SAVEDATA_DIALOG = 3, TROPHY_SETUP_DIALOG = 4;
constexpr int SCE_COMMON_DIALOG_STATUS_RUNNING = 1;
struct Dialog {
    int type = NO_DIALOG, status = 0;
};
namespace overlay {
struct vertex {
    float x, y, u, v;
};
struct common_dialog_overlay {
    int polls = 0;
    bool poll_dialog(Dialog &, int, int) {
        ++polls;
        return true;
    }
    bool input_loop_exited() { return false; }
    void reset_input_loop() {}
};
} // namespace overlay
struct Manager {
    int created = 0, attached = 0, removed = 0;
    std::shared_ptr<overlay::common_dialog_overlay> dialog;
    template <class T>
    std::shared_ptr<T> get() { return dialog; }
    template <class T>
    std::shared_ptr<T> create() {
        ++created;
        dialog = std::make_shared<T>();
        return dialog;
    }
    template <class T>
    void remove() {
        ++removed;
        dialog.reset();
    }
    template <class T>
    void attach_thread_input(const char *, std::shared_ptr<T>) { ++attached; }
};
struct State {
    Dialog *common_dialog = nullptr;
    Manager *overlay_manager = nullptr;
    int sys_date_format = 0, sys_button = 0;
    void route() {
        // INSERT_ROUTING
    }
};
namespace vk {
using DeviceSize = uint64_t;
enum class BufferUsageFlagBits { eVertexBuffer };
} // namespace vk
static bool draws_pending = false;
static int destroyed = 0;
namespace vkutil {
constexpr int vma_mapped_alloc = 0;
struct Buffer {
    size_t size = 0;
    bool buffer = false;
    int allocation = 1;
    std::unique_ptr<uint8_t[]> bytes;
    void *mapped_data = nullptr;
    Buffer() = default;
    explicit Buffer(size_t capacity)
        : size(capacity) {}
    void init_buffer(vk::BufferUsageFlagBits, int) {
        bytes = std::make_unique<uint8_t[]>(size);
        mapped_data = bytes.get();
        buffer = true;
    }
    void destroy() {
        assert(!draws_pending);
        if (buffer)
            ++destroyed;
        bytes.reset();
        mapped_data = nullptr;
        buffer = false;
    }
};
} // namespace vkutil
struct Command {
    std::vector<overlay::vertex> verts;
    int config = 0;
};
struct Prepared {
    struct {
        std::vector<Command> draw_commands;
    } compiled;
};
struct Allocator {
    std::vector<std::pair<size_t, size_t>> flushed;
    void flushAllocation(int, size_t offset, size_t size) { flushed.emplace_back(offset, size); }
};
struct GPU {
    Allocator allocator;
};
struct Overlay {
    std::vector<Prepared> m_prepared_views;
    std::vector<vkutil::Buffer> m_vertex_buffers = std::vector<vkutil::Buffer>(1);
    uint32_t m_active_frame_slot = 0;
    vk::DeviceSize m_vertex_buffer_offset = 0;
    GPU *m_state;
    void reserve() {
        // INSERT_RESERVE
    }
    void upload(const Command &draw_cmd) {
        // INSERT_UPLOAD
    }
};
int main() {
    Manager manager;
    Dialog dialog;
    State state{ &dialog, &manager };
    // Ordinary dialogs keep working, including their input handler.
    for (int type : { MESSAGE_DIALOG, SAVEDATA_DIALOG }) {
        dialog = { type, SCE_COMMON_DIALOG_STATUS_RUNNING };
        state.route();
        assert(manager.dialog && manager.attached == manager.created);
        dialog.status = 0;
        state.route();
        assert(!manager.dialog);
    }
    // A stale non-native overlay is removed when native text entry starts.
    dialog = { MESSAGE_DIALOG, SCE_COMMON_DIALOG_STATUS_RUNNING };
    state.route();
    const int attached = manager.attached;
    dialog.type = IME_DIALOG;
    state.route();
#ifdef VITA3K_PLATFORM_IOS
    assert(!manager.dialog && manager.attached == attached);
    const int created = manager.created;
    state.route();
    assert(manager.created == created);
#else
    assert(manager.dialog && manager.dialog->polls == 2);
#endif
    assert(dialog.type == IME_DIALOG && dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING);
    dialog.status = 0;
    state.route();
    assert(!manager.dialog);
    state.common_dialog = nullptr;
    state.route();

    GPU gpu;
    Overlay renderer;
    renderer.m_state = &gpu;
    renderer.reserve(); // empty frame does not allocate
    assert(!renderer.m_vertex_buffers[0].buffer);
    // Several dialogs cross the former 256 KiB growth point mid-frame.
    for (int frame = 0; frame < 3; ++frame) {
        renderer.m_prepared_views.clear();
        gpu.allocator.flushed.clear();
        renderer.m_vertex_buffer_offset = 0;
        const size_t count = frame == 0 ? 4 : 20000;
        Command command{ std::vector<overlay::vertex>(count, { 1.f, 2.f, 3.f, 4.f }) };
        renderer.m_prepared_views.push_back({ { { command, command } } });
        renderer.m_prepared_views.push_back({ { { command } } });
        draws_pending = false; // adapter for the slot's completed image fence
        renderer.reserve();
        auto *memory = renderer.m_vertex_buffers[0].mapped_data;
        const int previous_destroyed = destroyed;
        size_t offset = 0;
        for (const auto &view : renderer.m_prepared_views) {
            for (const auto &draw : view.compiled.draw_commands) {
                renderer.upload(draw);
                draws_pending = true;
                assert(renderer.m_vertex_buffers[0].mapped_data == memory);
                assert(destroyed == previous_destroyed);
                const size_t size = draw.verts.size() * sizeof(overlay::vertex);
                assert(std::memcmp(static_cast<uint8_t *>(memory) + offset, draw.verts.data(), size) == 0);
                assert(gpu.allocator.flushed.back() == std::make_pair(offset, size));
                offset += (size + 15) & ~size_t(15);
            }
        }
        assert(renderer.m_vertex_buffer_offset == offset);
        assert(offset <= renderer.m_vertex_buffers[0].size);
        assert(gpu.allocator.flushed.size() == 3);
    }
    assert(destroyed == 1); // grows once between frames, reused thereafter
}
