"""Real overlay placement and bounded log-tail producer/consumer policy."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class OverlayLayoutTests(unittest.TestCase):
    def test_rotation_clamping_and_log_tail(self):
        root = Path(__file__).resolve().parents[2]
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        start = main.index("namespace {\nconstexpr size_t kRecentLogLinesCapacity")
        ring = main[start:main.index("\nnamespace {", start + 1)]
        fixture = (Path(__file__).parent / "ios_overlay_layout.cpp").read_text().replace("// INSERT_RING", ring)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "overlay.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "overlay"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "ios/include"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
