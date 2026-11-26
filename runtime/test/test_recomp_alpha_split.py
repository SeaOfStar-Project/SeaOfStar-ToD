"""Without dual-source blending: the draws that go through one pass or two come out as the dual-source blend would make them."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).parent
RUNTIME = HERE.parent / "recomp/runtime"


@unittest.skipUnless(shutil.which("clang"), "needs clang")
class AlphaSplit(unittest.TestCase):
    def test_every_plan_matches_the_dual_source_blend(self):
        with tempfile.TemporaryDirectory() as folder:
            program = Path(folder) / "program"
            subprocess.run(["clang", str(HERE / "c/test_alpha_split.c"), "-w", "-O1", f"-I{RUNTIME}", f"-I{HERE}", "-lm", "-o", str(program)],
                           check=True, capture_output=True)
            result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.stdout, "ok\n", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
