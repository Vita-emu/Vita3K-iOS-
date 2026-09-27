"""Guest AVC Stop output, delayed-frame capacity and restart contract."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from test_avplayer_lifecycle import function


class AVCDrainTests(unittest.TestCase):
    def test_delayed_output_and_repeat_stop(self):
        root = Path(__file__).resolve().parents[2]
        module = (root / "vita3k/modules/SceVideodec/SceVideodecUser.cpp").read_text()
        declarations = module[module.index("enum {"):module.index("EXPORT(int, sceAvcdecCreateDecoder,")]
        source = (Path(__file__).parent / "avc_drain.cpp").read_text()
        source = source.replace("// INSERT_DECLARATIONS", declarations)
        source = source.replace("// INSERT_STOP", function(module, "EXPORT(int, sceAvcdecDecodeStop,"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "drain.cpp"
            path.write_text(source)
            binary = Path(directory) / "drain"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-DVITA3K_PLATFORM_IOS", str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
