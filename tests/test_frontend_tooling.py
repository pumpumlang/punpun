#!/usr/bin/env python3
import os
import re
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


    def test_unknown_name_exposes_machine_applicable_fixit(self):
        self.write('fn answer() -> i64 { return 42; }\nlaunch { say(answr()); }\n')
        result = self.run_ppc("check", "main.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error[E0201]", result.stderr)
        self.assertIn("did you mean `answer`?", result.stderr)
        self.assertIn("= fix-it:", result.stderr)
        self.assertIn("=> answer [machine-applicable]", result.stderr)

    def test_lint_rejects_duplicate_imports(self):
        self.write('bring std.math;\nbring std.math;\nlaunch { say(1); }\n')
        result = self.run_ppc("lint", "main.pp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("warning[W2002]", result.stderr)
        self.assertIn("duplicate import", result.stderr)


    def test_private_top_level_function_is_module_local(self):
        with open(os.path.join(self.temp, "helper.pp"), "w", encoding="utf-8") as handle:
            handle.write('private fn secret() -> i64 { return 7; }\npublic fn visible() -> i64 { return 8; }\n')
        self.write('bring helper;\nlaunch { say(secret()); }\n')
        result = self.run_ppc("check", "main.pp", "-I", self.temp)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error[E1304]", result.stderr)
        self.assertIn("private to module", result.stderr)
        self.assertIn("mark it `public fn`", result.stderr)


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
        self.assertIn(":scratch", debug.stdout)
        self.assertEqual(release.stdout.count("store $b2:scratch"), 1)

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

    def test_emit_machine_ir_has_explicit_abi_and_call_safe_allocation(self):
        self.write("""
            fn plus_one(value:i64) -> i64 { return value + 1; }
            fn keep_across_call(value:i64) -> i64 {
                return (value + 10) + plus_one(2);
            }
            launch { say(keep_across_call(5)); }
        """)
        machine = self.run_ppc("emit-machine-ir", "flow.pp")
        self.assertEqual(machine.returncode, 0, machine.stderr)
        self.assertIn("machine.target x86_64-sysv", machine.stdout)
        self.assertIn("machine.func @keep_across_call cc=punpun-block", machine.stdout)
        self.assertIn("arg0 value:int -> argblock+0 size=8", machine.stdout)
        self.assertIn("call plus_one", machine.stdout)
        self.assertIn("[cc=punpun-block args=8]", machine.stdout)
        call_live_lines = [line for line in machine.stdout.splitlines() if "call-live=yes" in line]
        self.assertTrue(call_live_lines, machine.stdout)
        for line in call_live_lines:
            self.assertNotRegex(line, r"-> (r10|r11|r8|r9|rcx|rdx|xmm\d+)$")

    def test_emit_abi_exposes_internal_record_return_contract(self):
        self.write("""
            struct Pair { public x:i64; public y:i64; }
            fn pair(x:i64,y:i64) -> Pair { return Pair(x:x,y:y); }
            launch { let p = pair(1,2); say(p.x); }
        """)
        abi = self.run_ppc("emit-abi", "flow.pp")
        self.assertEqual(abi.returncode, 0, abi.stderr)
        self.assertIn("punpun.abi.v1 target=x86_64-sysv", abi.stdout)
        self.assertIn("function pair cc=punpun-block", abi.stdout)
        self.assertIn("argblock=24 sret=yes", abi.stdout)
        self.assertIn("result type=Pair location=sret:argblock+0 size=16", abi.stdout)

    def test_cfg_crossing_values_use_distinct_spill_ranges(self):
        self.write("""
            enum Choice { One(i64), Two(i64, i64), }
            fn choose(c:Choice) -> i64 {
                return match c { One(v) => v, Two(a,b) => a + b, };
            }
            launch { say(choose(Choice::Two(4,5))); }
        """)
        machine = self.run_ppc("emit-machine-ir", "flow.pp", "--release")
        run = self.run_ppc("run", "flow.pp", "--release")
        self.assertEqual(machine.returncode, 0, machine.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "9\n")
        choose = machine.stdout.split("machine.func @choose", 1)[1].split("machine.func @main", 1)[0]
        slots = re.findall(r"gpr call-live=(?:yes|no) cfg-live=yes -> stack\[(\d+)", choose)
        self.assertGreaterEqual(len(slots), 3, choose)
        self.assertEqual(len(slots), len(set(slots)), choose)

    def test_machine_ir_spills_and_sizes_large_argument_block(self):
        self.write("""
            fn sum12(a:i64,b:i64,c:i64,d:i64,e:i64,f:i64,g:i64,h:i64,i:i64,j:i64,k:i64,l:i64) -> i64 {
                return a+b+c+d+e+f+g+h+i+j+k+l;
            }
            launch { say(sum12(1,2,3,4,5,6,7,8,9,10,11,12)); }
        """)
        machine = self.run_ppc("emit-machine-ir", "flow.pp")
        self.assertEqual(machine.returncode, 0, machine.stderr)
        self.assertIn("machine.func @sum12 cc=punpun-block", machine.stdout)
        self.assertIn("abi args=96 hidden-result=no", machine.stdout)
        self.assertIn("arg11 l:int -> argblock+88 size=8", machine.stdout)
        self.assertRegex(machine.stdout, r"frame slots=[1-9]\d* bytes=[1-9]\d*")
        self.assertRegex(machine.stdout, r"v\d+:int@stack\[\d+\]")

    def test_direct_x86_uses_machine_ir_body_for_scalar_cfg_and_calls(self):
        self.write("""
            fn plus_one(value:i64) -> i64 { return value + 1; }
            fn choose(value:i64) -> i64 {
                if value > 3 { return plus_one(value); }
                return value - 1;
            }
            launch { say(choose(5)); }
        """)
        asm = self.run_ppc("emit-asm", "flow.pp")
        run = self.run_ppc("run", "flow.pp")
        self.assertEqual(asm.returncode, 0, asm.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "6\n")
        self.assertIn("# body-lowering: machine-ir @plus_one", asm.stdout)
        self.assertIn("# body-lowering: machine-ir @choose", asm.stdout)
        self.assertIn("# body-lowering: machine-ir @main", asm.stdout)

    def test_direct_x86_short_circuit_is_machine_ir_lowered(self):
        self.write("""
            fn guarded(value:i64) -> bool {
                return value != 0 and (10 / value) > 1;
            }
            launch { say(guarded(0)); }
        """)
        asm = self.run_ppc("emit-asm", "flow.pp")
        run = self.run_ppc("run", "flow.pp")
        self.assertEqual(asm.returncode, 0, asm.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "no\n")
        self.assertIn("# body-lowering: machine-ir @guarded", asm.stdout)


    def test_direct_x86_all_supported_semantics_use_machine_ir(self):
        self.write("""
            struct Pair { public left: i64; public right: i64; }
            enum Choice { One(i64), Two(i64, i64), }
            fn make_pair(a:i64,b:i64) -> Pair { return Pair(left: a, right: b); }
            fn use_pair(p:Pair) -> i64 { return p.left + p.right; }
            fn choose(c:Choice) -> i64 {
                return match c { One(v) => v, Two(a,b) => a + b, };
            }
            launch {
                let p = make_pair(2,3);
                say(use_pair(p));
                say(choose(Choice::Two(4,5)));
                let mut xs = [1,2,3];
                xs[1] = 9;
                say(xs[1]);
            }
        """)
        asm = self.run_ppc("emit-asm", "flow.pp")
        run = self.run_ppc("run", "flow.pp")
        self.assertEqual(asm.returncode, 0, asm.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "5\n9\n9\n")
        self.assertNotIn("body-lowering: legacy-source", asm.stdout)
        for name in ("make_pair", "use_pair", "choose", "main"):
            self.assertIn(f"# body-lowering: machine-ir @{name}", asm.stdout)

    def test_machine_allocator_locations_drive_native_emission(self):
        self.write("""
            fn calc(a:i64,b:i64) -> i64 {
                let c = a + b;
                let d = c * 2;
                return d;
            }
            launch { say(calc(2,3)); }
        """)
        machine = self.run_ppc("emit-machine-ir", "flow.pp", "--release")
        asm = self.run_ppc("emit-asm", "flow.pp", "--release")
        run = self.run_ppc("run", "flow.pp", "--release")
        self.assertEqual(machine.returncode, 0, machine.stderr)
        self.assertEqual(asm.returncode, 0, asm.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, "10\n")
        self.assertRegex(machine.stdout, r"v\d+:int@r1[0-5]")
        # r10/r11/r12 are allocator homes, not instruction-selection scratch.
        self.assertRegex(asm.stdout, r"\bmov r1[0-5], rax\b")
