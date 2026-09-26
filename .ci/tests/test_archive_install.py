"""Real ZIP/SFO transactions and license validation with a synthetic PFS decoder."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class ArchiveInstallTests(unittest.TestCase):
    def test_vpk_nonpdrm_transactions(self):
        root = Path(__file__).resolve().parents[2]
        fixture = (Path(__file__).parent / "archive_install.cpp").read_text()
        license_header = (root / "vita3k/packages/include/packages/license.h").read_text()
        fixture = fixture.replace("// INSERT_LICENSE_STRUCT", license_header[
            license_header.index("struct SceNpDrmLicense {"):license_header.index("struct License {")])
        license_source = (root / "vita3k/packages/src/license.cpp").read_text()
        fixture = fixture.replace("// INSERT_LICENSE_READ_COPY", license_source[
            license_source.index("static bool open_license("):license_source.index("void get_license(")])
        pkg = (root / "vita3k/packages/src/pkg.cpp").read_text()
        fixture = fixture.replace("// INSERT_DECRYPT", pkg[
            pkg.index("bool decrypt_install_nonpdrm("):pkg.index("bool install_pkg(")])
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        fixture = fixture.replace("// INSERT_LICENSE_WORKER", main[
            main.index("void start_license_import("):main.index("void start_import(")])
        start = main.index("const auto prepare = [&emuenv, job]")
        fixture = fixture.replace("// INSERT_PREPARE", main[start:main.index("const auto progress = [job]", start)])
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            source = scratch / "fixture.cpp"
            source.write_text(fixture)
            miniz = scratch / "miniz.o"
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-c",
                str(root / "external/miniz/miniz.c"), "-o", str(miniz)], check=True)
            binary = scratch / "fixture"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                "-I", str(root / "vita3k/packages/include"), "-I", str(root / "external/miniz"),
                "-I", str(root / ".ci/tests/ir_interpreter/stubs"),
                str(source), str(root / "vita3k/packages/src/archive.cpp"),
                str(root / "vita3k/packages/src/sfo.cpp"), str(miniz), "-o", str(binary)], check=True)
            subprocess.run([str(binary), directory], check=True, timeout=60)
