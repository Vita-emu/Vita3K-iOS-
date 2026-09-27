"""Production graphics cache policy and command ordering with recording GPU calls."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class GraphicsSettingsTests(unittest.TestCase):
    def test_surface_cache_barriers_samplers_and_present_modes(self):
        root = Path(__file__).resolve().parents[2]
        surface = (root / "vita3k/renderer/src/vulkan/surface_cache.cpp").read_text()
        screen = (root / "vita3k/renderer/src/vulkan/screen_renderer.cpp").read_text()
        cache = (root / "vita3k/renderer/src/texture/cache.cpp").read_text()
        source = (Path(__file__).parent / "graphics_settings.cpp").read_text()
        header = (root / "vita3k/renderer/include/renderer/vulkan/surface_cache.h").read_text()
        start = header.index("struct CastedTexture {")
        source = source.replace("// INSERT_CAST_TYPE", header[start:header.index("\n};", start) + 3])
        env = (root / "vita3k/emuenv/include/emuenv/state.h").read_text()
        counter = next(line for line in env.splitlines() if "frame_count{" in line)
        source = source.replace("// INSERT_COUNTER", counter)
        start = screen.index("void ScreenRenderer::select_present_mode()")
        source = source.replace("// INSERT_PRESENT", screen[start:screen.index("void ScreenRenderer::create_swapchain", start)])
        start = cache.index("int TextureCache::cache_and_bind_sampler(")
        source = source.replace("// INSERT_SAMPLER", cache[start:cache.index("\n} // namespace renderer", start)])
        start = surface.index("        CastedTexture *casted = nullptr;")
        source = source.replace("// INSERT_CAST_LOOKUP", surface[start:surface.index("        // use prerender cmd", start)])
        start = surface.index("        casted->scene_timestamp = scene_timestamp;")
        end = surface.index("        casted->texture.transition_to(cmd_buffer, vkutil::ImageLayout::ColorAttachmentReadWrite);", start)
        source = source.replace("// INSERT_COPY", surface[start:end])
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "graphics.cpp"
            fixture.write_text(source)
            binary = Path(directory) / "graphics"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", str(fixture), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
