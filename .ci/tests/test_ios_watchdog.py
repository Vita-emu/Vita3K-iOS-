"""Run the production boot watchdog with a simulated clock/frame source."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class WatchdogTests(unittest.TestCase):
    def test_healthy_frames_and_stalled_boot(self):
        root = Path(__file__).resolve().parents[2]
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        start = main.index("    std::thread guest_watchdog([&] {")
        body = main[start:main.index("\n    });", start)]
        body = body[body.index("        using namespace"):]
        body = body.replace("std::this_thread::sleep_for", "advance_clock")
        fixture = (Path(__file__).parent / "ios_watchdog.cpp").read_text().replace("// INSERT_WATCHDOG", body)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "watchdog.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "watchdog"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source),
                "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
