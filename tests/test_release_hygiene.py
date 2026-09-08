#!/usr/bin/env python3
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReleaseHygieneTests(unittest.TestCase):
    def test_source_privacy_and_debris_audit(self):
        result = subprocess.run(
            ["python3", str(ROOT / "scripts" / "privacy_audit.py"), str(ROOT)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("privacy audit passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
