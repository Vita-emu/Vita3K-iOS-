"""Run the production texture-cache bind path with a recording upload backend."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class TextureLifetimeTests(unittest.TestCase):
    def test_released_movie_texture_and_concurrent_free(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        cache = (root / "vita3k/renderer/src/texture/cache.cpp").read_text()
        fixture = (Path(__file__).parent / "texture_lifetime.cpp").read_text()
        start = cache.index("static bool texture_range_allocated(")
        fixture = fixture.replace("// INSERT_RANGE_CHECKS", cache[start:cache.index("\n#endif", start)])
        start = cache.index("static constexpr TextureGxmDataRepr default_texture_mask")
        fixture = fixture.replace("// INSERT_CACHE_BIND", cache[start:cache.index("\nint TextureCache::cache_and_bind_sampler", start)])
        vulkan = (root / "vita3k/renderer/src/vulkan/texture.cpp").read_text()
        start = vulkan.index("        if (!context.state.texture_cache.cache_and_bind_texture(")
        fixture = fixture.replace("// INSERT_VULKAN_FAILURE", vulkan[start:vulkan.index("        auto &image", start)])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "texture.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "texture"
            subprocess.run(compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror",
                "-pthread", "-DVITA3K_PLATFORM_IOS", "-I", str(root / "vita3k/mem/include"),
                str(source), str(root / "vita3k/mem/src/allocator.cpp"),
                "-Wno-parentheses", "-Wno-sign-compare", "-Wno-ignored-qualifiers", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)
