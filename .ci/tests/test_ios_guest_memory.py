"""Run production guest allocation/free against a recording host VM adapter."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class GuestMemoryTests(unittest.TestCase):
    def test_free_host_pages_and_reuse(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        root = Path(__file__).resolve().parents[2]
        memory = (root / "vita3k/mem/src/mem.cpp").read_text()
        fixture = (Path(__file__).parent / "ios_guest_memory.cpp").read_text()
        start = memory.index("static Address alloc_inner(MemState &state", memory.index("bool prereserve_guest_memory()"))
        fixture = fixture.replace("// INSERT_ALLOCATE", memory[start:memory.index("\nAddress alloc_aligned", start)])
        start = memory.index("static void decommit_guest_pages(")
        fixture = fixture.replace("// INSERT_FREE", memory[start:memory.index("\nuint32_t mem_available", start)])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "guest_memory.cpp"
            source.write_text(fixture)
            for platform in ("ios", "posix"):
                with self.subTest(platform=platform):
                    binary = Path(directory) / platform
                    defines = ["-DVITA3K_PLATFORM_IOS"] if platform == "ios" else []
                    # The unchanged allocator has legacy signed/cast/parenthesis warnings.
                    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-type-limits", "-Wno-sign-compare", "-Wno-ignored-qualifiers",
                        "-Wno-parentheses", "-pthread"] + defines + [
                        "-I", str(root / "vita3k/mem/include"), str(source),
                        str(root / "vita3k/mem/src/allocator.cpp"), "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)
