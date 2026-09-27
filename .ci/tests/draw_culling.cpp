#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <renderer/draw_safety.h>
#include <util/ios_runtime_tuning.h>
#include <util/render_diagnostics.h>
#define LOG_WARN(...) ((void)0)
struct MemState {};
struct Config {};
using SceGxmPrimitiveType = int;
using SceGxmIndexFormat = int;
constexpr int SCE_GXM_REGION_CLIP_NONE = 0, SCE_GXM_REGION_CLIP_ALL = 1,
              SCE_GXM_REGION_CLIP_OUTSIDE = 2, SCE_GXM_REGION_CLIP_INSIDE = 3;
template <typename T>
struct Ptr {
    T *pointer = nullptr;
    T *get(MemState &) const { return pointer; }
};
namespace vk {
struct Rect2D {
    struct {
        int32_t x = 0, y = 0;
    } offset;
    struct {
        uint32_t width = 0, height = 0;
    } extent;
};
} // namespace vk
struct Program {
    uint32_t size = sizeof(Program), program_flags = 0;
    uint32_t primary_program_instr_count = 2, secondary_program_instr_count = 2;
    bool secondary = true;
    uint64_t primary[2]{}, secondary_code[2]{};
    const uint64_t *primary_program_start() const { return primary; }
    const uint64_t *secondary_program_start() const { return secondary_code; }
    bool is_secondary_program_available() const { return secondary; }
};
struct Vertex {
    struct {
        bool can_skip_when_clipped = true;
    } data;
    decltype(data) *renderer_data = &data;
};
struct VKContext {
    struct Target {
        uint32_t width = 960, height = 544;
    } target;
    Target *render_target = &target;
    struct {
        float res_multiplier = 1;
        struct {
            bool support_shader_interlock = true;
        } features;
        struct {
            int retrieve_render_pass(int, bool, bool, bool) { return 1; }
        } pipeline_cache;
    } state;
    Vertex vertex;
    struct Record {
        struct {
            int x = 0, y = 0;
        } region_clip_min, region_clip_max;
        int region_clip_mode = SCE_GXM_REGION_CLIP_NONE;
        bool viewport_flat = true;
        Ptr<Vertex> vertex_program;
        struct {
            bool data = false;
        } color_surface;
    } record;
    vk::Rect2D scissor{};
    struct {
        vk::Rect2D last{};
        void setScissor(int, vk::Rect2D value) { last = value; }
    } render_cmd;
    bool is_recording = true, in_renderpass = false, is_first_scene_draw = true;
    bool ignore_macroblock = false, is_in_query = false;
    void *current_visibility_buffer = nullptr;
    int current_render_pass = 0, current_color_format = 0;
    int clears = 0, submitted = 0;
    void check_for_macroblock_change(bool) {}
    void start_render_pass() {
        in_renderpass = true;
        ++clears;
    }
    VKContext() { record.vertex_program.pointer = &vertex; }
};
// INSERT_CLIP
// INSERT_DRAW_PREFIX
int main() {
    using renderer::intersect_clip;
    auto clip = intersect_clip(-10, -20, 100, 100, 960, 544);
    assert(clip.x == 0 && clip.y == 0 && clip.width == 90 && clip.height == 80);
    clip = intersect_clip(-100, -100, 10, 10, 960, 544);
    assert(clip.width == 0 && clip.height == 0);
    clip = intersect_clip(950, 540, 100, 100, 960, 544);
    assert(clip.width == 10 && clip.height == 4);
    clip = intersect_clip(1000, 600, 100, 100, 960, 544);
    assert(clip.width == 0 && clip.height == 0);
    clip = intersect_clip(std::numeric_limits<int32_t>::min(), 0,
        std::numeric_limits<uint32_t>::max(), 100, 960, 544);
    assert(clip.width == 960);

    Program program;
    constexpr uint32_t store_flag = 1 << 14;
    assert(renderer::vertex_program_can_skip(program, store_flag));
    program.primary[1] = uint64_t(0b11110) << 59;
    assert(!renderer::vertex_program_can_skip(program, store_flag));
    program.primary[1] = 0;
    program.secondary_code[0] = uint64_t(0b11101) << 59;
    assert(!renderer::vertex_program_can_skip(program, store_flag));
    program.secondary_code[0] = 0;
    program.program_flags = store_flag;
    assert(!renderer::vertex_program_can_skip(program, store_flag));
    program.program_flags = 0;
    program.primary_program_instr_count = 0xffffffff;
    assert(!renderer::vertex_program_can_skip(program, store_flag));
    program.primary_program_instr_count = 2;
    program.size = 1;
    assert(!renderer::vertex_program_can_skip(program, store_flag));

    VKContext context;
    MemState mem;
    Config config;
    context.record.region_clip_mode = SCE_GXM_REGION_CLIP_OUTSIDE;
    context.record.region_clip_min = { -10, -20 };
    context.record.region_clip_max = { 89, 79 };
    sync_clipping(context);
    assert(context.scissor.extent.width == 90 && context.scissor.extent.height == 80);
    context.state.res_multiplier = 0.5;
    sync_clipping(context);
    assert(context.scissor.extent.width == 45 && context.scissor.extent.height == 40);
    context.record.region_clip_mode = SCE_GXM_REGION_CLIP_ALL;
    sync_clipping(context);
    assert(context.scissor.extent.width == 0);
    render_diagnostics::mode = 1;
    ios_runtime::tuning.conservative_culling = true;
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 0 && context.clears == 1 && !context.is_first_scene_draw);
    assert(render_diagnostics::counters[render_diagnostics::Culled] == 1);
    context.vertex.data.can_skip_when_clipped = false;
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 1); // possible vertex storage side effects
    context.vertex.data.can_skip_when_clipped = true;
    context.current_visibility_buffer = &context;
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 2); // query bookkeeping preserved
    context.current_visibility_buffer = nullptr;
    context.is_in_query = true;
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 3);
    context.is_in_query = false;
    ios_runtime::tuning.conservative_culling = false;
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 4);
    ios_runtime::tuning.conservative_culling = true;
    context.record.region_clip_mode = SCE_GXM_REGION_CLIP_NONE;
    sync_clipping(context);
    draw(context, 0, 0, {}, 6, 1, mem, config);
    assert(context.submitted == 5); // visible geometry retained
    draw(context, 0, 0, {}, 0, 1, mem, config);
    draw(context, 0, 0, {}, 6, 0, mem, config);
    assert(context.submitted == 5 && context.clears == 1);
}
