#!/usr/bin/env python3
"""`pp --update`, the toolchain updater.

Only the parts that can be decided without a network are exercised here: the
version ordering, and the refusal to unpack a release over a source checkout.
The download path needs the real release feed and is not simulated, because a
fake feed would test the fake rather than the script.
"""
import os
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "punpun"


def extract_function(name: str) -> str:
    """Lift one shell function out of the launcher so it can be run alone."""
    text = LAUNCHER.read_text(encoding="utf-8")
    match = re.search(rf"^{name}\(\) \{{\n(.*?)^\}}$", text, re.M | re.S)
    if not match:
        raise AssertionError(f"{name}() is not defined in {LAUNCHER}")
    return f"{name}() {{\n{match.group(1)}}}\n"


class VersionOrderTests(unittest.TestCase):
    def order(self, left: str, right: str) -> str:
        script = extract_function("version_order") + f'version_order "{left}" "{right}"\n'
        result = subprocess.run(["sh", "-c", script], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_orders_releases(self):
        self.assertEqual(self.order("1.3.0", "1.4.0"), "older")
        self.assertEqual(self.order("1.4.0", "1.3.0"), "newer")
        self.assertEqual(self.order("1.4.0", "1.4.0"), "same")

    def test_compares_numerically_not_lexically(self):
        # The classic trap: "1.10.0" sorts before "1.9.0" as text.
        self.assertEqual(self.order("1.9.0", "1.10.0"), "older")
        self.assertEqual(self.order("1.10.0", "1.9.0"), "newer")

    def test_ignores_a_leading_v(self):
        self.assertEqual(self.order("1.4.0", "v1.4.0"), "same")
        self.assertEqual(self.order("v1.3.0", "1.4.0"), "older")


class SourceCheckoutTests(unittest.TestCase):
    def test_source_checkout_defers_to_git(self):
        # Unpacking a release over a checkout would silently discard local work,
        # so the updater refuses and names the command that is correct here.
        result = subprocess.run([str(LAUNCHER), "--update"], cwd=ROOT, text=True,
                                capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("source checkout", result.stdout)
        self.assertIn("git", result.stdout)

    def test_update_is_listed_separately_from_the_package_command(self):
        # `pp update` resolves the dependency graph; `pp --update` replaces the
        # toolchain. The help has to keep them apart or one will be typed for
        # the other.
        result = subprocess.run([str(LAUNCHER), "--help"], cwd=ROOT, text=True,
                                capture_output=True, timeout=60)
        self.assertIn("pp --update", result.stdout)
        self.assertIn("toolchain itself", result.stdout)


class InstalledLayoutTests(unittest.TestCase):
    def test_refuses_an_unverified_installer(self):
        # A release with no published checksums must not be installed on trust.
        script = LAUNCHER.read_text(encoding="utf-8")
        self.assertIn("refusing to install unverified", script)
        self.assertIn("checksum mismatch", script)

    def test_needs_a_downloader(self):
        script = LAUNCHER.read_text(encoding="utf-8")
        self.assertIn("--update needs curl or wget", script)


if __name__ == "__main__":
    unittest.main()
