"""Production JIT admission/run/step paths with a synthetic instruction runner."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class ExecutionThreadTests(unittest.TestCase):
    def test_bounded_execution_and_release(self):
        root = Path(__file__).resolve().parents[2]
        cpu = (root / "vita3k/cpu/src/dynarmic_cpu.cpp").read_text()
        fixture = (Path(__file__).parent / "ios_execution_threads.cpp").read_text()
        start = cpu.index("static util::ExecutionGate &ios_cpu_execution_gate()")
        fixture = fixture.replace("// INSERT_GATE", cpu[start:cpu.index("\n#endif", start)])
        start = cpu.index("    uint64_t GetTicksRemaining() override")
        fixture = fixture.replace("// INSERT_TICKS", cpu[start:cpu.index("\n};", start)])
        start = cpu.index("int DynarmicCPU::run()")
        fixture = fixture.replace("// INSERT_RUN", cpu[start:cpu.index("bool DynarmicCPU::hit_breakpoint()", start)])
        start = cpu.index("    config.enable_cycle_counting = false;")
        fixture = fixture.replace("// INSERT_CYCLE_CONFIG", cpu[start:cpu.index("\n#endif", start) + len("\n#endif")])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "threads.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "threads"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror",
                "-DVITA3K_PLATFORM_IOS", "-I", str(root / "vita3k/util/include"),
                str(source), "-o", str(binary)], check=True)
            for limit in (0, 1, 2, 4, 8):
                with self.subTest(limit=limit):
                    subprocess.run([str(binary), str(limit)], check=True, timeout=20)
