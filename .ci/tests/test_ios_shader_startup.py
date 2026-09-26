"""Exercise actual launch preparation with disk-cache and precompile combinations."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class ShaderStartupTests(unittest.TestCase):
    def test_precompilation_preserves_disk_cache(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        app = (root / "vita3k/app/src/app.cpp").read_text()
        start = app.index("void prepare_game_launch_overlay(")
        function = app[start:app.index("\nbool update_runtime_metrics", start)]
        source = r'''
#include <util/ios_runtime_tuning.h>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
namespace fs = std::filesystem;
namespace fs_utils { std::string path_to_utf8(const fs::path &p) { return p.string(); } }
namespace renderer {
struct State {
    std::string precompile_bg_path = "old";
    std::vector<int> precompile_queue{99}, shaders_cache_hashs;
    int precompile_total = 99, precompile_progress = 99, reads = 0;
    bool precompile_requested = true, index_available = false;
    std::atomic<bool> precompile_complete{true};
};
bool get_shaders_cache_hashs(State &s) {
    ++s.reads;
    if (s.index_available) s.shaders_cache_hashs = {1, 2};
    return s.index_available;
}
}
struct EmuEnvState {
    renderer::State *renderer = nullptr;
    fs::path vita_fs_path;
    struct { std::string app_path = "synthetic-title"; } io;
    struct { struct { bool shader_cache = false; } current_config; } cfg;
};
// INSERT_FUNCTION
int main() {
    EmuEnvState empty;
    prepare_game_launch_overlay(empty);
    for (bool disk : {false, true}) for (bool precompile : {false, true}) for (bool index : {false, true}) {
        renderer::State state;
        EmuEnvState env;
        env.renderer = &state;
        env.cfg.current_config.shader_cache = disk;
        state.index_available = index;
        ios_runtime::tuning.precompile_shaders = precompile;
        prepare_game_launch_overlay(env);
        assert(state.reads == (disk ? 1 : 0));
        assert(state.shaders_cache_hashs.size() == (disk && index ? 2u : 0u));
        assert(env.cfg.current_config.shader_cache == disk);
#ifdef VITA3K_PLATFORM_IOS
        const bool expected = disk && index && precompile;
#else
        const bool expected = disk && index;
#endif
        assert(state.precompile_requested == expected);
        assert(state.precompile_queue.size() == (expected ? 2u : 0u));
        assert(!state.precompile_complete && state.precompile_total == 0 && state.precompile_progress == 0);
    }
}
'''.replace("// INSERT_FUNCTION", function)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            fixture = work / "startup.cpp"
            fixture.write_text(source)
            for platform in ("ios", "desktop"):
                with self.subTest(platform=platform):
                    binary = work / platform
                    defines = ["-DVITA3K_PLATFORM_IOS"] if platform == "ios" else []
                    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror"] + defines + [
                        "-I", str(root / "vita3k/util/include"), str(fixture), "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], cwd=work, check=True)
