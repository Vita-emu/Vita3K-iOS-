"""Host checks for text completion and corrupt-cache rejection; no Apple SDK."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class IOSRuntimeGuardTests(unittest.TestCase):
    def test_text_completion_and_cache_guards(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        # Compile the actual coordinator, replacing only its platform/core
        # dependencies. This exercises stale replies and event ordering without
        # pretending that UIKit, guest callbacks or the GPU ran on this host.
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        start = main.index("class IOSInputSession {")
        end = main.index("\n};", start) + 3
        coordinator = main[start:end]
        fixture = (Path(__file__).parent / "ios_runtime_guards.cpp").read_text()
        source_text = fixture.replace("// INSERT_COORDINATOR", coordinator)
        ime_source = (root / "vita3k/modules/SceIme/SceIme.cpp").read_text()
        start = ime_source.index("    // These are union members:")
        end = ime_source.index("    const auto arg", start)
        source_text = source_text.replace("// INSERT_EVENT_PAYLOAD", ime_source[start:end])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "guards.cpp"
            binary = Path(directory) / "guards"
            source.write_text(source_text)
            subprocess.run(compiler + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(root / "ios/include"),
                "-I", str(root / "vita3k/renderer/include"),
                str(source), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
