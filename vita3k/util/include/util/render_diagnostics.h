// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace render_diagnostics {
// Configured once before threads start. No collection, timers or allocation
// when disabled. Counts are interval totals, not measurements of GPU time.
inline int mode = 0;
enum Counter : unsigned {
    Draws,
    FlatDraws,
    Culled,
    PendingDraws,
    FeedbackDraws,
    Passes,
    SurfaceCopies,
    TextureUploads,
    Swapchains,
    PipelineQueued,
    PipelineCompiles,
    PipelineFailures,
    CompileMicroseconds,
    Count
};
inline std::array<std::atomic<uint64_t>, Count> counters{};
inline std::atomic<unsigned> shader_samples{ 4 };
inline void add(Counter counter, uint64_t amount = 1) {
    if (mode)
        counters[counter].fetch_add(amount, std::memory_order_relaxed);
}
inline bool take_shader_sample() {
    if (mode != 2)
        return false;
    unsigned left = shader_samples.load(std::memory_order_relaxed);
    while (left && !shader_samples.compare_exchange_weak(left, left - 1, std::memory_order_relaxed)) {
    }
    return left != 0;
}
struct Snapshot {
    uint64_t interval_ms = 0;
    std::array<uint64_t, Count> values{};
};
// Called by the existing frontend metrics tick. Fixed storage; no extra
// polling thread, per-draw log formatting, shader dump or GPU synchronization.
class Reporter {
    uint64_t last_ms;

public:
    explicit Reporter(uint64_t now_ms)
        : last_ms(now_ms) {
        // Each game gets a fresh reporting interval; do not attribute the
        // previous title's last partial interval to the next title.
        for (auto &counter : counters)
            counter.exchange(0, std::memory_order_relaxed);
        shader_samples.store(4, std::memory_order_relaxed);
    }
    bool poll(uint64_t now_ms, Snapshot &snapshot) {
        if (!mode || now_ms < last_ms || now_ms - last_ms < 5000)
            return false;
        snapshot.interval_ms = now_ms - last_ms;
        last_ms = now_ms;
        for (unsigned i = 0; i < Count; ++i)
            snapshot.values[i] = counters[i].exchange(0, std::memory_order_relaxed);
        shader_samples.store(4, std::memory_order_relaxed);
        return true;
    }
};
class CompileTimer {
    std::chrono::steady_clock::time_point start{};

public:
    CompileTimer() {
        if (mode) {
            start = std::chrono::steady_clock::now();
        }
    }
    ~CompileTimer() {
        if (mode) {
            add(PipelineCompiles);
            add(CompileMicroseconds, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        }
    }
};
} // namespace render_diagnostics
