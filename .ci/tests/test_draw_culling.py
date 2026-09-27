"""Production clipping and pre-draw culling with a recording context."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class DrawCullingTests(unittest.TestCase):
    def test_clip_intersection_and_side_effect_guards(self):
        root = Path(__file__).resolve().parents[2]
        sync = (root / "vita3k/renderer/src/vulkan/sync_state.cpp").read_text()
        scene = (root / "vita3k/renderer/src/vulkan/scene.cpp").read_text()
        fixture = (Path(__file__).parent / "draw_culling.cpp").read_text()
        start = sync.index("void sync_clipping(")
        fixture = fixture.replace("// INSERT_CLIP", sync[start:sync.index("void sync_stencil_func(", start)])
        start = scene.index("void draw(VKContext")
        end = scene.index("    const SceGxmFragmentProgram &gxm_fragment_program", start)
        fixture = fixture.replace("// INSERT_DRAW_PREFIX", scene[start:end] + "(void)indices_ptr; (void)type; (void)format; (void)config; ++context.submitted; }\n")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "culling.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "culling"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "vita3k/renderer/include"), "-I", str(root / "vita3k/util/include"),
                str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
