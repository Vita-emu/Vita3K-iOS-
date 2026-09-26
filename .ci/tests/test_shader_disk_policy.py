"""Exercise the production disk loader's cache toggle and feature variant key."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class ShaderDiskPolicyTests(unittest.TestCase):
    def test_cache_toggle_and_accuracy_variant(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        fmt = root / "external/dynarmic/externals/fmt/include"
        if not compiler or not shutil.which(compiler[0]) or not (fmt / "fmt/format.h").exists():
            self.skipTest("Requires host compiler and pinned Dynarmic bundled fmt headers")
        text = (root / "vita3k/renderer/src/vulkan/pipeline_cache.cpp").read_text()
        start = text.index("vk::ShaderModule PipelineCache::load_shader_from_disk(")
        loader = text[start:text.index("\n} // namespace", start)]
        fixture = r'''
#include <fmt/format.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
namespace fs = std::filesystem;
using Sha256Hash = std::array<uint8_t, 32>;
std::string hex_string(const Sha256Hash &) { return "synthetic-hash"; }
namespace shader { constexpr int CURRENT_VERSION = 99; }
namespace vk {
struct ShaderModule {
    int value = 0;
    ShaderModule(std::nullptr_t) {}
    ShaderModule(int value_) : value(value_) {}
};
struct ShaderModuleCreateInfo { size_t codeSize; const uint32_t *pCode; };
}
int reads = 0;
namespace renderer {
std::vector<uint32_t> pre_load_shader_spirv(const fs::path &path) {
    ++reads;
    uint32_t value = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char *>(&value), sizeof(value))) return {};
    return {value};
}
}
struct Device {
    int creates = 0;
    vk::ShaderModule createShaderModule(vk::ShaderModuleCreateInfo info) {
        ++creates;
        assert(info.codeSize == sizeof(uint32_t));
        return static_cast<int>(*info.pCode);
    }
};
struct State {
    std::atomic<bool> use_disk_shader_cache{false};
    fs::path shaders_path;
    Device device;
    int features = 0x42;
    int get_features_mask() const { return features; }
};
struct PipelineCache { State state; vk::ShaderModule load_shader_from_disk(const Sha256Hash &); };
// LOADER
int main(int argc, char **argv) {
    assert(argc == 2);
    PipelineCache cache;
    cache.state.shaders_path = argv[1];
    const auto write = [&](const char *name, uint32_t value) {
        std::ofstream file(cache.state.shaders_path / name, std::ios::binary);
        file.write(reinterpret_cast<const char *>(&value), sizeof(value));
    };
    write("vk99-synthetic-hash.spv", 100); // legacy variant must never load
    write("vk99-f42-synthetic-hash.spv", 42);
    write("vk99-f43-synthetic-hash.spv", 43);
    assert(cache.load_shader_from_disk({}).value == 0 && reads == 0);
    cache.state.use_disk_shader_cache = true;
    assert(cache.load_shader_from_disk({}).value == 42);
    cache.state.features = 0x43;
    assert(cache.load_shader_from_disk({}).value == 43);
    cache.state.features = 0x44;
    assert(cache.load_shader_from_disk({}).value == 0);
    cache.state.use_disk_shader_cache = false;
    assert(cache.load_shader_from_disk({}).value == 0 && reads == 3);
    assert(cache.state.device.creates == 2);
}
'''.replace("// LOADER", loader)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "loader.cpp"
            source.write_text(fixture)
            binary = work / "loader"
            subprocess.run(compiler + ["-std=c++17", "-DFMT_HEADER_ONLY", "-Wall", "-Wextra", "-Werror",
                                       "-I", str(fmt), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(work)], check=True)
