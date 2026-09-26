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
static_assert(ios_jit_cache_size_for_memory(2 * gib) == 12 * mib);
static_assert(ios_jit_cache_size_for_memory(3 * gib) == 12 * mib);
static_assert(ios_jit_cache_size_for_memory(3 * gib + 1) == 16 * mib);
static_assert(ios_jit_cache_size_for_memory(4 * gib) == 16 * mib);
static_assert(ios_jit_cache_size_for_memory(std::numeric_limits<std::uint64_t>::max()) == 16 * mib);

int main() {
    for (const auto memory : std::array<std::uint64_t, 6>{0, 1, 2 * gib, 3 * gib, 4 * gib, 8 * gib}) {
        const auto cache = ios_jit_cache_size_for_memory(memory);
        // Dynarmic's documented limits and the 16 KiB device page alignment.
        if (cache <= 8 * mib || cache > 128 * mib || cache % 16384 != 0)
            return 1;
    }
}
''')
            subprocess.run(compiler + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "vita3k/cpu/include"), str(source), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
