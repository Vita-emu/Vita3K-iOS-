"""Bounded renderer telemetry with real counters and concurrent producers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class RenderDiagnosticsTests(unittest.TestCase):
    def test_bounded_summary_and_shader_sampling(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "diagnostics"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "vita3k/util/include"),
                str(Path(__file__).parent / "render_diagnostics.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
