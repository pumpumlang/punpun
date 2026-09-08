#!/usr/bin/env python3
"""0.6 specification, compatibility and version-contract regression tests."""
from __future__ import annotations

import json
import shutil
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

    def test_generic_functions_types_and_methods_execute_on_both_backends(self):
        expected = "9\n12\ngeneric\n14\ncontract\n"
        for extra in ((), ("--cc-backend",)):
            with self.subTest(backend=extra or ("direct",)):
                result = run("run", FIXTURES / "generics_run.pp", *extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, expected)

    def test_monomorphization_is_visible_and_deduplicated_in_hir(self):
        result = run("emit-hir", FIXTURES / "generics_run.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("func @identity$3_int$("), 1)
        self.assertEqual(result.stdout.count("func @identity$3_str$("), 1)

    def test_enums_nested_patterns_option_result_and_propagation_execute(self):
        expected = "3\n4\n7\n55\n66\n"
        for extra in ((), ("--cc-backend",)):
            with self.subTest(backend=extra or ("direct",)):
                result = run("run", FIXTURES / "enums_run.pp", *extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, expected)

    def test_enum_ast_is_no_longer_feature_gated(self):
        result = run("emit-ast", FIXTURES / "enums_run.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Enum Choice<T>", result.stdout)
        self.assertIn("Variant Value(T)", result.stdout)
        self.assertIn("Match", result.stdout)

    def test_non_exhaustive_match_reports_missing_variant(self):
        result = run("check", FIXTURES / "non_exhaustive.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E1716", result.stderr)
        self.assertIn("missing Busy", result.stderr)

    def test_generic_constraint_failure_is_diagnostic(self):
        result = run("check", FIXTURES / "generic_constraint_error.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E1604", result.stderr)
        self.assertIn("does not satisfy constraint 'Copy'", result.stderr)

    def test_option_result_stdlib_example_runs_on_both_backends(self):
        expected = "PunPun\n42\n7\n"
        example = ROOT / "examples/generics-and-results.pp"
        for extra in ((), ("--cc-backend",)):
            result = run("run", example, *extra)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, expected)

    def test_safe_slices_execute_on_both_backends(self):
        expected = "2\n20\n30\n"
        for extra in ((), ("--cc-backend",)):
            with self.subTest(backend=extra or ("direct",)):
                result = run("run", FIXTURES / "slices_run.pp", *extra, "--no-cache")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, expected)

    @unittest.skipUnless(shutil.which("clang"), "Clang/LLVM is required for the optional LLVM backend")
    def test_optional_llvm_backend_and_ir_emission(self):
        result = run("run", FIXTURES / "slices_run.pp", "--llvm-backend", "--no-cache")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "2\n20\n30\n")
        ir = run("emit-llvm", FIXTURES / "generics_run.pp")
        self.assertEqual(ir.returncode, 0, ir.stderr)
        self.assertIn("target triple", ir.stdout)
        self.assertIn("define", ir.stdout)

    def test_move_state_joins_report_maybe_moved(self):
        result = run("check", FIXTURES / "maybe_moved_error.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E0706", result.stderr)
        self.assertIn("may have been moved", result.stderr)

    def test_live_slice_prevents_owner_mutation(self):
        result = run("check", FIXTURES / "borrow_mutation_error.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E0707", result.stderr)

    def test_local_slice_cannot_escape_owner(self):
        result = run("check", FIXTURES / "slice_escape_error.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("E0702", result.stderr)

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

    def test_new_diagnostics_have_explanations(self):
        for code in ("E1604", "E1716"):
            self.assertTrue((ROOT / "docs/errors" / f"{code}.md").is_file())


if __name__ == "__main__":
    unittest.main()
