// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace renderer {
struct ClipRect {
    int32_t x, y;
    uint32_t width, height;
};
// Intersect in signed 64-bit arithmetic; unsigned subtraction at negative
// origins expands the rectangle and can wrap. Clamp to the attachment too.
inline ClipRect intersect_clip(int32_t x, int32_t y, uint32_t width, uint32_t height,
    uint32_t target_width, uint32_t target_height) {
    const int64_t left = std::clamp<int64_t>(x, 0, target_width);
    const int64_t top = std::clamp<int64_t>(y, 0, target_height);
    const int64_t right = std::clamp<int64_t>(int64_t(x) + width, left, target_width);
    const int64_t bottom = std::clamp<int64_t>(int64_t(y) + height, top, target_height);
    return { static_cast<int32_t>(left), static_cast<int32_t>(top),
        static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) };
}

// Cache this decision at vertex-program creation, never scan shader code per
// draw. Reject both USSE load/store opcode families (same masks as the shader
// analyzer) and the buffer-store flag; false negatives are deliberately safe.
// Malformed ranges are not eligible for skipping.
template <typename Program>
bool vertex_program_can_skip(const Program &program, uint32_t store_flag) {
    if (program.program_flags & store_flag)
        return false;
    const auto base = reinterpret_cast<uintptr_t>(&program);
    const auto safe_range = [&](const void *code, uint32_t count) {
        const auto address = reinterpret_cast<uintptr_t>(code);
        if (address < base || address - base > program.size
            || uint64_t(count) * sizeof(uint64_t) > program.size - (address - base))
            return false;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t instruction;
            std::memcpy(&instruction, static_cast<const uint8_t *>(code) + i * sizeof(uint64_t), sizeof(instruction));
            const auto op = instruction >> 59;
            if (op == 0b11101 || op == 0b11110)
                return false;
        }
        return true;
    };
    if (!safe_range(program.primary_program_start(), program.primary_program_instr_count))
        return false;
    return !program.is_secondary_program_available()
        || safe_range(program.secondary_program_start(), program.secondary_program_instr_count);
}
inline bool skip_clipped_draw(bool enabled, bool empty_draw, bool empty_clip,
    bool vertex_safe, bool visibility_active) {
    return enabled && !visibility_active && (empty_draw || (empty_clip && vertex_safe));
}
} // namespace renderer
