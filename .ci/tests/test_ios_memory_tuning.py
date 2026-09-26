"""Exercise tuning boundaries and real surface retirement with fake GPU handles."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class IOSMemoryTuningTests(unittest.TestCase):
    def test_memory_policy_and_surface_reuse(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        surface = (root / "vita3k/renderer/src/vulkan/surface_cache.cpp").read_text()
        source = (Path(__file__).parent / "ios_memory_tuning.cpp").read_text()
        start = surface.index("static void wait_for_surface_readbacks(VKState &state)")
        source = source.replace("// INSERT_READBACK_BARRIER", surface[start:surface.index("\nvoid VKSurfaceCache::destroy_surface(ColorSurfaceCacheInfo", start)])
        start = surface.index("void VKSurfaceCache::destroy_surface(ColorSurfaceCacheInfo &info)")
        source = source.replace("// INSERT_SURFACE_RETIREMENT", surface[start:surface.index("\nvk::ImageView VKSurfaceCache::retrieve_sampled_view", start)])
        start = surface.index("void VKSurfaceCache::destroy_surface(DepthStencilSurfaceCacheInfo &info)")
        source = source.replace("// INSERT_DEPTH_RETIREMENT", surface[start:surface.index("\nVKSurfaceCache::VKSurfaceCache", start)])
        start = surface.index("            const uint64_t row_bytes =")
        source = source.replace("// INSERT_READBACK_SIZE", surface[start:surface.index("\n            copy_buffer.init_buffer", start)])
        texture = (root / "vita3k/renderer/src/vulkan/texture.cpp").read_text()
        start = texture.index("    const bool need_wait =")
        source = source.replace("// INSERT_FENCE_CHECK", texture[start:texture.index("\n    const vk::Fence current_fence", start)])
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "memory.cpp"
            binary = Path(directory) / "memory"
            fixture.write_text(source)
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(root / "vita3k/util/include"), str(fixture), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
