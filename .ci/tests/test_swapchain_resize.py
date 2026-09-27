"""Exercise production extent selection and rebuild decisions with a fake surface."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class SwapchainResizeTests(unittest.TestCase):
    def test_distinct_window_and_surface_extents(self):
        root = Path(__file__).resolve().parents[2]
        screen = (root / "vita3k/renderer/src/vulkan/screen_renderer.cpp").read_text()
        fixture = (Path(__file__).parent / "swapchain_resize.cpp").read_text()
        start = screen.index("    surface_capabilities = state.physical_device.getSurfaceCapabilitiesKHR(surface);", screen.index("void ScreenRenderer::create_swapchain()"))
        fixture = fixture.replace("// INSERT_EXTENT", screen[start:screen.index("    swapchain_size =", start)])
        start = screen.index("bool ScreenRenderer::ensure_swapchain()")
        fixture = fixture.replace("// INSERT_REBUILD", screen[start:screen.index("\n} // namespace renderer::vulkan", start)])
        start = screen.index("bool window_has_drawable_size(")
        fixture = fixture.replace("// INSERT_VISIBLE", screen[start:screen.index("\n} // namespace", start)])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "resize.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "resize"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
