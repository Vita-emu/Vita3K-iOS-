// Vita3K emulator project
// Copyright (C) 2026 Vita3K team

#pragma once

#include <cstddef>
#include <cstdint>

// Keep headroom above Dynarmic's approximate 8 MiB minimum. Unknown memory
// sizes retain the existing budget; a failed hardware query must not select
// the smaller cache. This is cache capacity, not resident-memory usage.
constexpr std::size_t ios_jit_cache_size_for_memory(std::uint64_t physical_memory) {
    constexpr std::uint64_t low_memory_limit = 3ULL * 1024 * 1024 * 1024;
    constexpr std::size_t mib = 1024 * 1024;
    return physical_memory > 0 && physical_memory <= low_memory_limit ? 12 * mib : 16 * mib;
}
