"""Check the pinned ARM64 prelude and patched cache reset without executing JIT code."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class SmallJITTests(unittest.TestCase):
    def test_arm64_prelude_and_cache_reset(self):
        root = Path(__file__).resolve().parents[2]
        dynarmic = root / "external/dynarmic"
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler")
        if not (dynarmic / "src/dynarmic/backend/arm64/address_space.cpp").is_file():
            self.skipTest("Initialize the pinned external/dynarmic submodule")
        patch = root / "ios/patches/0001-oaknut-ios-rwx-jit.patch"
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            # Apply the complete shipped patch to pristine, pinned source files.
            for line in patch.read_text().splitlines():
                if line.startswith("--- a/"):
                    name = line[6:]
                    target = work / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(subprocess.check_output(["git", "show", "HEAD:" + name], cwd=dynarmic))
            subprocess.run(["git", "apply", str(patch)], cwd=work, check=True)
            base = work / "src/dynarmic/backend/arm64"
            header = (base / "address_space.h").read_text()
            self.assertIn("virtual void ClearCache();", header)
            a32 = (base / "a32_address_space.cpp").read_text()
            address = (base / "address_space.cpp").read_text()
            self.assertIn("if (GetRemainingSize() < 1024 * 1024) {\n        ClearCache();", address)
            fixture = (Path(__file__).parent / "ios_small_jit.cpp").read_text()
            def section(text, start, end):
                offset = text.index(start)
                return text[offset:text.index(end, offset)]
            fixture = fixture.replace("// INSERT_PRELUDE_INFO", section(header, "    struct PreludeInfo {", "\n};"))
            fixture = fixture.replace("// INSERT_TRAMPOLINES", section(a32, "template<auto mfp, typename T>", "A32AddressSpace::A32AddressSpace"))
            fixture = fixture.replace("// INSERT_PRELUDE", section(a32, "void A32AddressSpace::EmitPrelude()", "EmitConfig A32AddressSpace::GetEmitConfig()"))
            fixture = fixture.replace("// INSERT_CLEAR_BASE", section(address, "void AddressSpace::ClearCache()", "void AddressSpace::DumpDisassembly()"))
            fixture = fixture.replace("// INSERT_CLEAR_A32", section(a32, "void A32AddressSpace::ClearCache()", "void A32AddressSpace::InvalidateCacheRanges"))
            a64 = (base / "a64_address_space.cpp").read_text()
            fixture = fixture.replace("// INSERT_CLEAR_A64", section(a64, "void A64AddressSpace::ClearCache()", "void A64AddressSpace::InvalidateCacheRanges"))
            for name in ("a32", "a64"):
                self.assertIn("void ClearCache() override;", (base / (name + "_address_space.h")).read_text())
            source = work / "small_jit.cpp"
            source.write_text(fixture)
            binary = work / "small_jit"
            includes = [dynarmic / "src"] + [dynarmic / "externals" / name / "include" for name in ("oaknut", "mcl", "fmt")]
            arguments = [flag for path in includes for flag in ("-I", str(path))]
            subprocess.run(compiler + ["-std=c++20", "-DFMT_HEADER_ONLY", "-O1"] + arguments + [
                str(source), str(dynarmic / "src/dynarmic/backend/arm64/abi.cpp"),
                str(dynarmic / "src/dynarmic/backend/arm64/exclusive_monitor.cpp"),
                str(dynarmic / "externals/mcl/src/assert.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
