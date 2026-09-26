"""Host regression for native IME routing and overlay vertex-buffer lifetimes."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class DialogRenderingTests(unittest.TestCase):
    def test_dialog_routing_and_frame_vertex_storage(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        renderer = (root / "vita3k/renderer/src/renderer.cpp").read_text()
        begin = renderer.index("    if (common_dialog) {")
        routing = renderer[begin:renderer.index("\n}\n\nvoid State::init_overlay_font_dirs", begin)]
        overlay = (root / "vita3k/renderer/src/vulkan/overlay_renderer.cpp").read_text()
        begin = overlay.index("    vk::DeviceSize required = 0;", overlay.index("void OverlayRenderer::prepare("))
        reserve = overlay[begin:overlay.index("\n}\n\nvoid OverlayRenderer::render", begin)]
        begin = overlay.index("    if (draw_cmd.verts.empty())", overlay.index("void OverlayRenderer::draw_command("))
        upload = overlay[begin:overlay.index("    vk::ImageView tex_2d_view;", begin)]
        fixture = (Path(__file__).parent / "ios_dialog_rendering.cpp").read_text()
        fixture = fixture.replace("// INSERT_ROUTING", routing).replace("// INSERT_RESERVE", reserve).replace("// INSERT_UPLOAD", upload)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "dialog.cpp"
            source.write_text(fixture)
            for platform in ("ios", "desktop"):
                with self.subTest(platform=platform):
                    binary = Path(directory) / platform
                    defines = ["-DVITA3K_PLATFORM_IOS"] if platform == "ios" else []
                    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-variable"] + defines + [str(source), "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)
