#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PPC = os.path.join(ROOT, "build", "ppc")


class FrontendToolingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.mkdtemp(prefix="punpun-frontend-")
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)

    def write(self, text):
        path = os.path.join(self.temp, "main.pp")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def run_ppc(self, *args):
        return subprocess.run([PPC, *args], cwd=self.temp, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

    def test_tokens_expose_byte_spans(self):
        self.write('launch:\n    say "hi"\ndone\n')
        result = self.run_ppc("emit-tokens", "main.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('word  [0..6)  "launch"', result.stdout)
        self.assertIn('string', result.stdout)

    def test_native_and_c_backends_emit_source_mappings(self):
        self.write('launch {\n    say("mapped");\n}\n')
        assembly = self.run_ppc("emit-asm", "main.pp")
        portable = self.run_ppc("emit-c", "main.pp")
        self.assertEqual(assembly.returncode, 0, assembly.stderr)
        self.assertEqual(portable.returncode, 0, portable.stderr)
        self.assertIn('.file 1 "', assembly.stdout)
        self.assertRegex(assembly.stdout, r"\.loc 1 2 \d+")
        self.assertIn('#line 2 "', portable.stdout)

    def test_ast_dump_contains_real_nodes(self):
        self.write('launch:\n    pin x <- 2 + 3\n    say x\ndone\n')
        result = self.run_ppc("emit-ast", "main.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Function main() -> int", result.stdout)
        self.assertIn('Binary value="+"', result.stdout)
        self.assertIn("Variable name=x", result.stdout)

    def test_modern_diagnostic_has_code_source_and_caret(self):
        self.write('launch:\n    pin x as int <- "wrong"\ndone\n')
        result = self.run_ppc("check", "main.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error[E1200]", result.stderr)
        self.assertIn('pin x as int <- "wrong"', result.stderr)
        self.assertIn("^", result.stderr)

    def test_formatter_is_idempotent_and_checkable(self):
        path = self.write('launch:   \n       when yes:  \n say "x"\n       done\n done   \n')
        first = self.run_ppc("fmt", "main.pp")
        self.assertEqual(first.returncode, 0, first.stderr)
        with open(path, encoding="utf-8") as handle:
            once = handle.read()
        second = self.run_ppc("fmt", "main.pp")
        self.assertEqual(second.returncode, 0, second.stderr)
        with open(path, encoding="utf-8") as handle:
            twice = handle.read()
        self.assertEqual(once, twice)
        check = self.run_ppc("fmt", "main.pp", "--check")
        self.assertEqual(check.returncode, 0, check.stderr)
        self.assertEqual(once, 'launch:\n    when yes:\n        say "x"\n    done\ndone\n')


if __name__ == "__main__":
    unittest.main(verbosity=2)

class HirTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="punpun-hir-")
        self.addCleanup(shutil.rmtree, self.directory, ignore_errors=True)

    def write(self, text):
        path = os.path.join(self.directory, "flow.pp")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(textwrap.dedent(text).lstrip())
        return path

    def run_ppc(self, *args):
        return subprocess.run([PPC, *args], cwd=self.directory, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

    def test_typed_hir_has_cfg_and_values(self):
        self.write("""
            craft choose(x as int) gives int:
                when x > 2:
                    give x + 1
                otherwise:
                    give x - 1
                done
            done
            launch:
                say choose(5)
            done
        """)
        result = self.run_ppc("emit-hir", "flow.pp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("func @choose(x: int) -> int", result.stdout)
        self.assertRegex(result.stdout, r"%\d+:bool = binary >")
        self.assertIn("branch %", result.stdout)
        self.assertIn("return %", result.stdout)

    def test_release_hir_runs_optimization_pipeline(self):
        self.write("""
            launch:
                when 2 + 3 == 5:
                    say "folded"
                otherwise:
                    say "dead"
                done
            done
        """)
        debug = self.run_ppc("emit-hir", "flow.pp")
        release = self.run_ppc("emit-hir", "flow.pp", "--release")
        self.assertEqual(debug.returncode, 0, debug.stderr)
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertIn("branch %", debug.stdout)
        self.assertNotIn("branch %", release.stdout)
        self.assertNotIn("const dead", release.stdout)
        self.assertIn("const folded", release.stdout)

    def test_release_hir_eliminates_duplicate_values_and_dead_stores(self):
        self.write("""
            fn repeated(x: i64) -> i64 {
                let mut scratch = 1;
                scratch = 2;
                let first = x + 7;
                let second = x + 7;
                return first + second + scratch;
            }
            launch { say(repeated(4)); }
        """)
        debug = self.run_ppc("emit-hir", "flow.pp")
        release = self.run_ppc("emit-hir", "flow.pp", "--release")
        self.assertEqual(debug.returncode, 0, debug.stderr)
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertGreater(debug.stdout.count("binary +"), release.stdout.count("binary +"))
        self.assertIn("store scratch", debug.stdout)
        self.assertEqual(release.stdout.count("store scratch"), 1)

    def test_optimization_preserves_program_result(self):
        self.write("""
            fn repeated(x: i64) -> i64 {
                let mut scratch = 1;
                scratch = 2;
                let first = x + 7;
                let second = x + 7;
                return first + second + scratch;
            }
            launch { say(repeated(4)); }
        """)
        debug = self.run_ppc("run", "flow.pp")
        release = self.run_ppc("run", "flow.pp", "--release")
        self.assertEqual(debug.returncode, 0, debug.stderr)
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertEqual(debug.stdout, "24\n")
        self.assertEqual(release.stdout, debug.stdout)

    def test_emit_ir_has_mir_liveness_and_register_allocation(self):
        self.write("""
            fn sum8(a:i64,b:i64,c:i64,d:i64,e:i64,f:i64,g:i64,h:i64) -> i64 {
                return a+b+c+d+e+f+g+h;
            }
            launch { say(sum8(1,2,3,4,5,6,7,8)); }
        """)
        hir = self.run_ppc("emit-hir", "flow.pp")
        mir = self.run_ppc("emit-ir", "flow.pp")
        self.assertEqual(hir.returncode, 0, hir.stderr)
        self.assertEqual(mir.returncode, 0, mir.stderr)
        self.assertIn("func @sum8", hir.stdout)
        self.assertIn("mir.func @sum8", mir.stdout)
        self.assertIn("liveness:", mir.stdout)
        self.assertRegex(mir.stdout, r"v\d+:int@(r1[0-5]|spill\[\d+\])")
