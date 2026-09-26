"""Run the production shader acquisition paths with a deterministic fake GPU."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class ShaderWorkerTests(unittest.TestCase):
    def test_shader_workers(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        source = (root / "vita3k/renderer/src/vulkan/pipeline_cache.cpp").read_text()
        start = source.index("vk::PipelineShaderStageCreateInfo PipelineCache::retrieve_shader(")
        retrieve = source[start:source.index("\nvk::RenderPass PipelineCache::", start)]
        start = source.index("vk::ShaderModule PipelineCache::precompile_shader(")
        precompile = source[start:source.index("\nvk::ShaderModule PipelineCache::load_shader_from_disk", start)]
        fixture = (root / ".ci/tests/shader_workers.cpp").read_text()
        fixture = fixture.replace("// PRODUCTION_FUNCTIONS", retrieve + precompile)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            cpp = work / "shader_workers.cpp"
            cpp.write_text(fixture)
            binary = work / "shader_workers"
            subprocess.run(compiler + ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                                       "-Wno-unused-parameter", "-pthread", str(cpp), "-o", str(binary)],
                           check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=20)
