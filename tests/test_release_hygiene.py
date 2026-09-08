#!/usr/bin/env python3
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from privacy_audit import forbidden_filename


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

    def test_timestamped_backups_and_editor_debris_are_forbidden(self):
        forbidden = [
            "logo.png.bak-20260908170333",
            "thing.bak",
            "merge.orig",
            "merge.rej",
            "scratch.swp",
            "scratch.swo",
            "notes~",
        ]
        for name in forbidden:
            with self.subTest(name=name):
                self.assertTrue(forbidden_filename(Path(name)))


if __name__ == "__main__":
    unittest.main()
