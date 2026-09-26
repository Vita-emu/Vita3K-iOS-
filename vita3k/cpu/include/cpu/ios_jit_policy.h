// Vita3K emulator project
// Copyright (C) 2026 Vita3K team

#pragma once

#include <cstddef>
#include <cstdint>

// The ARM64 backend reserves 1 MiB for emission and checks prelude headroom.
// Unknown memory sizes retain the existing budget. This is code capacity,
// not resident-memory usage or a quota for all per-thread JIT metadata.
constexpr std::size_t ios_jit_cache_size_for_memory(std::uint64_t physical_memory) {
    constexpr std::uint64_t low_memory_limit = 3ULL * 1024 * 1024 * 1024;
    constexpr std::size_t mib = 1024 * 1024;
    return physical_memory > 0 && physical_memory <= low_memory_limit ? 8 * mib : 16 * mib;
}
