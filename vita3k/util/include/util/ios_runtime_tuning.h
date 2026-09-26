// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

namespace ios_runtime {
// Loaded once by the iOS frontend before renderer/JIT initialization. Settings
// edits affect the next process: existing JIT pool regions must keep their size.
enum class CPUBackend { Jit = 0,
    IRInterpreter = 1 };

struct Tuning {
    CPUBackend cpu_backend = CPUBackend::Jit;
    int guest_memory_mib = 768;
    int jit_cache_mib = 0;
    int shader_workers = 0;
    int texture_entries = 0;
    bool trim_staging_buffers = true;
    bool precompile_shaders = false;
};
inline Tuning tuning;

constexpr CPUBackend cpu_backend(int requested) {
    return requested == 1 ? CPUBackend::IRInterpreter : CPUBackend::Jit;
}
inline bool uses_jit() { return tuning.cpu_backend == CPUBackend::Jit; }
constexpr int guest_memory_mib(int requested) {
    return requested == 512 || requested == 768 || requested == 1024 ? requested : 768;
}
constexpr int jit_cache_mib(int requested) {
    return requested == 4 || requested == 8 || requested == 12 || requested == 16 || requested == 24 || requested == 32 ? requested : 0;
}
constexpr int shader_workers(int requested, int logical_cores) {
    const int available = logical_cores > 0 ? logical_cores : 1;
    return requested >= 1 && requested <= 4 ? (requested < available ? requested : available) : 0;
}
constexpr int texture_entries(int requested, int memory_mib) {
    if (requested == 128 || requested == 256 || requested == 512)
        return requested;
    return memory_mib > 0 && memory_mib <= 3072 ? 128 : 512;
}
// Hysteresis prevents frequent allocation/free when texture sizes fluctuate.
constexpr bool shrink_staging(unsigned long long capacity, unsigned long long required,
    unsigned long long frame, unsigned long long last_resize) {
    return capacity > 4ULL * 1024 * 1024 && required <= capacity / 4
        && frame >= last_resize && frame - last_resize >= 120;
}
} // namespace ios_runtime
