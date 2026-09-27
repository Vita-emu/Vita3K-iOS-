"""Exercise production player lifecycle functions with a synthetic decoder."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


class AvPlayerLifecycleTests(unittest.TestCase):
    def test_stop_close_and_concurrent_decode(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        module = (root / "vita3k/modules/SceAvPlayer/SceAvPlayer.cpp").read_text()
        codec = (root / "vita3k/codec/src/player.cpp").read_text()
        fixture = (Path(__file__).parent / "avplayer_lifecycle.cpp").read_text()
        pointer = (root / "vita3k/mem/include/mem/ptr.h").read_text()
        start = pointer.index("template <class T>\nclass Ptr {")
        end = pointer.index("static_assert(sizeof(Ptr<const void>)", start)
        end = pointer.index(";", end) + 1
        fixture = fixture.replace("// INSERT_PTR", pointer[start:end])
        declarations = module[module.index("struct PlayerInfoState;"):module.index("static inline uint64_t current_time()")]
        fixture = fixture.replace("// INSERT_DECLARATIONS", declarations)
        fixture = fixture.replace("// INSERT_FRAMERATE", function(codec, "uint64_t PlayerState::get_framerate_microseconds()"))
        functions = function(module, "static inline uint64_t current_time()")
        functions += function(module, "static PlayerPtr find_player(")
        functions += function(module, "static Ptr<uint8_t> get_buffer(")
        for result, name in [("int", "Close"), ("int", "Stop"), ("bool", "GetVideoData"),
                             ("bool", "GetAudioData"), ("int", "Pause"), ("int", "Resume")]:
            functions += function(module, f"EXPORT({result}, sceAvPlayer{name},")
        fixture = fixture.replace("// INSERT_FUNCTIONS", functions)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "avplayer.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "avplayer"
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-pthread",
                str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)
