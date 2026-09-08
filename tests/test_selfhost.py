#!/usr/bin/env python3
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SelfHostingCompilerTests(unittest.TestCase):
    def test_fixed_point_and_compiled_example(self):
        result = subprocess.run(
            [str(ROOT / "selfhost" / "bootstrap.sh")],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        out = ROOT / "build" / "selfhost"
        self.assertEqual(
            (out / "ppc-self-stage1.c").read_bytes(),
            (out / "ppc-self-stage2.c").read_bytes(),
        )
        hello = subprocess.run(
            [str(out / "hello")],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertEqual(hello.returncode, 0, hello.stderr)
        self.assertEqual(hello.stdout, "hello from self-hosted PunPun, world\n")

    def test_native_bool_return_abi_is_normalized(self):
        source = ROOT / "build" / "selfhost" / "bool-abi.pp"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(
            'fn truth() -> bool { return contains("same", "same"); }\n'
            'launch { if !truth() { return 9; } if !file_exists("selfhost/ppc_self.pp") { return 8; } println("bool ABI ok"); return 0; }\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            [str(ROOT / "build" / "ppc"), "go", str(source), "--no-cache"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("bool ABI ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
