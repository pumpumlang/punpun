#!/usr/bin/env python3
"""0.6 specification, compatibility and version-contract regression tests."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC = ROOT / "build/ppc"
FIXTURES = ROOT / "tests/fixtures/language_0_6"
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def run(*args: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(PPC), *(str(arg) for arg in args)], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
    )


class VersionContractTests(unittest.TestCase):
    def test_all_executable_version_surfaces_match_root(self):
        subprocess.run([sys.executable, "scripts/check_version.py"], cwd=ROOT, check=True)

    def test_release_workflow_has_no_previous_release_path(self):
        workflow = (ROOT / ".github/workflows/platform-release.yml").read_text(encoding="utf-8")
        self.assertNotIn("release-0.5.0-beta", workflow)
        self.assertIn("PUNPUN_VERSION", workflow)

    def test_language_info_exposes_reserved_0_6_keywords(self):
        result = subprocess.run([str(PPC), "language-info"], cwd=ROOT, check=True,
                                text=True, stdout=subprocess.PIPE)
        info = json.loads(result.stdout)
        self.assertEqual(info["compiler_version"], VERSION)
        for keyword in ("enum", "match", "case", "where"):
            self.assertIn(keyword, info["keywords"])


class LanguageSurfaceContractTests(unittest.TestCase):
    def test_generic_declaration_and_nested_types_parse_into_ast(self):
        result = run("emit-ast", FIXTURES / "generic_surface.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Shape Pair<T: Copy, U>", result.stdout)
        self.assertIn("Result<U,Option<T>>", result.stdout)
        self.assertIn("Function identity<T: Copy + Comparable<T>>", result.stdout)

    def test_enum_syntax_has_stable_feature_gate(self):
        result = run("check", FIXTURES / "enum_pending.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E0900", result.stderr)
        self.assertIn("0.6 Step 3", result.stderr)

    def test_match_syntax_has_stable_feature_gate(self):
        result = run("check", FIXTURES / "match_pending.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E0900", result.stderr)
        self.assertIn("0.6 Step 3", result.stderr)

    def test_0_5_modern_and_migration_sources_remain_valid(self):
        for fixture in ("compat_modern.pp", "compat_legacy.pp"):
            with self.subTest(fixture=fixture):
                result = run("check", FIXTURES / fixture)
                self.assertEqual(result.returncode, 0, result.stderr)


class SpecificationContractTests(unittest.TestCase):
    def test_required_decisions_are_checked_in(self):
        expected = {
            "syntax.md": ("Generic declarations", "Algebraic enums", "Matching and destructuring"),
            "types-and-generics.md": ("Monomorphization", "Overload resolution", "Constraints"),
            "enums-and-matching.md": ("Option<T>", "Result<T, E>", "Exhaustiveness"),
            "ownership.md": ("Move-state analysis", "Deterministic `Drop`", "Borrowing"),
            "compatibility.md": ("Source compatibility", "Required gates"),
        }
        for filename, needles in expected.items():
            text = (ROOT / "spec/0.6" / filename).read_text(encoding="utf-8")
            for needle in needles:
                self.assertIn(needle, text, f"{filename} lacks {needle}")


if __name__ == "__main__":
    unittest.main()
