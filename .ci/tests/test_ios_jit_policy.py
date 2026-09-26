"""Compile the shared device RAM policy without requiring an Apple SDK."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class IOSJITPolicyTests(unittest.TestCase):
    def test_cache_budgets_and_device_memory_boundaries(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "policy.cpp"
            binary = Path(directory) / "policy"
            source.write_text(r'''
#include <cpu/ios_jit_policy.h>
#include <array>
#include <limits>

constexpr std::uint64_t gib = 1024ULL * 1024 * 1024;
constexpr std::size_t mib = 1024 * 1024;
static_assert(ios_jit_cache_size_for_memory(0) == 16 * mib);
static_assert(ios_jit_cache_size_for_memory(2 * gib) == 8 * mib);
static_assert(ios_jit_cache_size_for_memory(3 * gib) == 8 * mib);
static_assert(ios_jit_cache_size_for_memory(3 * gib + 1) == 16 * mib);
static_assert(ios_jit_cache_size_for_memory(4 * gib) == 16 * mib);
static_assert(ios_jit_cache_size_for_memory(std::numeric_limits<std::uint64_t>::max()) == 16 * mib);

int main() {
    for (const auto memory : std::array<std::uint64_t, 6>{0, 1, 2 * gib, 3 * gib, 4 * gib, 8 * gib}) {
        const auto cache = ios_jit_cache_size_for_memory(memory);
        // ARM64 emission reserve and the 16 KiB device page alignment.
        if (cache < 4 * mib || cache > 128 * mib || cache % 16384 != 0)
            return 1;
    }
}
''')
            subprocess.run(compiler + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "vita3k/cpu/include"), str(source), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_production_selection_reaches_small_device_caches(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        cpu = (root / "vita3k/cpu/src/dynarmic_cpu.cpp").read_text()
        start = cpu.index("std::size_t ios_jit_code_cache_size() {")
        function = cpu[start:cpu.index("\n}\n#endif", start) + 2]
        prefix = r'''
#include <cpu/ios_jit_policy.h>
#include <util/ios_runtime_tuning.h>
#include <cassert>
#include <string>
#undef __aarch64__
// INSERT_ARCH
#define LOG_INFO(...) ((void)0)
[[maybe_unused]] static std::uint64_t hardware_memory;
#ifdef __aarch64__
static int sysctlbyname(const char *, void *value, size_t *size, void *, size_t) {
    assert(*size == sizeof(hardware_memory));
    *static_cast<std::uint64_t *>(value) = hardware_memory;
    return 0;
}
#endif
'''
        suffix = r'''
int main(int argc, char **argv) {
    assert(argc == 4);
    ios_runtime::tuning.jit_cache_mib = std::stoi(argv[1]);
    hardware_memory = std::stoull(argv[2]) * 1024 * 1024;
    assert(ios_jit_code_cache_size() == std::stoull(argv[3]) * 1024 * 1024);
    // Existing executable mappings must not change size after preferences change.
    ios_runtime::tuning.jit_cache_mib = 32;
    assert(ios_jit_code_cache_size() == std::stoull(argv[3]) * 1024 * 1024);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            for architecture in ("arm64", "x64"):
                source = Path(directory) / (architecture + ".cpp")
                binary = Path(directory) / architecture
                define = "#define __aarch64__ 1" if architecture == "arm64" else ""
                source.write_text(prefix.replace("// INSERT_ARCH", define) + function + suffix)
                subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "vita3k/cpu/include"), "-I", str(root / "vita3k/util/include"),
                    str(source), "-o", str(binary)], check=True)
                for request, memory in [(size, 3072) for size in (4, 8, 12, 16, 24, 32, 0, 3, -1)] + [(0, 0), (0, 3073)]:
                    valid = request in (4, 8, 12, 16, 24, 32)
                    if architecture == "arm64":
                        expected = request if valid else (8 if 0 < memory <= 3072 else 16)
                    else:
                        expected = max(request, 12) if valid else 16
                    subprocess.run([str(binary), str(request), str(memory), str(expected)], check=True)
