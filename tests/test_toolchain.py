#!/usr/bin/env python3
"""End-to-end test suite for the PunPun 0.5.0-beta toolchain.

This suite drives the real ``build/ppc`` compiler as an external process. Every
fixture (source file or package project)
is created inside an isolated temporary directory that is removed automatically,
so the repository tree is never modified and no ``.punpun`` artifacts leak.

Design notes
------------
* Commands are invoked through ``subprocess`` with argument *lists* and never a
  shell, so arguments containing spaces or shell metacharacters are passed
  through verbatim and cannot be reinterpreted.
* Every invocation has a timeout so a hung compiler or program fails the test
  instead of blocking the run.
* Programs that intentionally panic abort via ``SIGABRT``.  ``resource`` is used
  to set ``RLIMIT_CORE`` to zero for the compiler and its children so those
  aborts do not litter the working tree with core dumps.
* The absolute path to ``build/ppc`` is resolved relative to
  this file, so the suite is independent of the current working directory and
  does not require a globally installed toolchain.

Run directly (``python3 tests/test_toolchain.py``) or through ``tests/run.sh``.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

try:
    import resource
except ImportError:  # pragma: no cover - Punpun targets Linux only.
    resource = None


# --------------------------------------------------------------------------
# Toolchain and repository layout, resolved relative to this file.
# --------------------------------------------------------------------------
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS_DIR)
BUILD = os.path.join(ROOT, "build")
PPC = os.path.join(BUILD, "ppc")
EXAMPLES = os.path.join(ROOT, "examples")
RUNTIME = os.environ.get("PUNPUN_RUNTIME", os.path.join(ROOT, "runtime"))
LIBPUNPUN = os.path.join(RUNTIME, "libpunpun.a")

# Compilation invokes an external ``cc`` (with LTO in release), so give builds a
# generous ceiling; program execution and checks are fast.
BUILD_TIMEOUT = 120
RUN_TIMEOUT = 60

# SIGABRT-based panics surface as 128 + 6 through ``ppc go`` (which forwards the
# child's termination signal as an exit status).
PANIC_STATUS = 134


def _drop_core_dumps():
    """preexec hook: disable core dumps in spawned processes (panic tests)."""
    if resource is not None:
        try:
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        except (ValueError, OSError):  # pragma: no cover - best effort only.
            pass


# Also lower the limit for this process so any inherited default is zero even if
# ``preexec_fn`` is unavailable on an exotic platform.
_drop_core_dumps()


class ToolchainTestCase(unittest.TestCase):
    """Base class with helpers for isolated fixtures and command execution."""

    # -- fixture management -------------------------------------------------
    def project(self, prefix="punpun-test-"):
        """Create a fresh temp directory that is cleaned up after the test."""
        path = tempfile.mkdtemp(prefix=prefix)
        self.addCleanup(shutil.rmtree, path, ignore_errors=True)
        return path

    def write(self, directory, name, content):
        """Write ``content`` (dedented) to ``directory/name`` and return path."""
        path = os.path.join(directory, name)
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        text = textwrap.dedent(content)
        if text.startswith("\n"):
            text = text[1:]
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    # -- command execution --------------------------------------------------
    def run_command(self, argv, cwd, timeout=RUN_TIMEOUT, stdin=None):
        """Run ``argv`` in ``cwd`` with no shell; fail the test on timeout."""
        try:
            return subprocess.run(
                argv,
                cwd=cwd,
                input=stdin,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout,
                preexec_fn=_drop_core_dumps,
            )
        except subprocess.TimeoutExpired as error:
            self.fail(
                "command timed out after {}s: {}\nstdout={!r}\nstderr={!r}".format(
                    timeout, argv, error.stdout, error.stderr
                )
            )

    def ppc(self, *args, cwd, timeout=BUILD_TIMEOUT):
        return self.run_command([PPC, *args], cwd=cwd, timeout=timeout)

    # -- higher level source helpers ---------------------------------------
    def build_and_run(self, source, args=(), release=False, extra=None):
        """Compile+run a single-file program with ``ppc go`` in a temp dir."""
        directory = self.project()
        self.write(directory, "prog.pp", source)
        argv = [PPC, "go", "prog.pp"]
        if release:
            argv.append("--release")
        if extra:
            argv.extend(extra)
        if args:
            argv.append("--")
            argv.extend(str(a) for a in args)
        return self.run_command(argv, cwd=directory, timeout=BUILD_TIMEOUT)

    def check_source(self, source):
        """Frontend-only validation of a single-file program with ``ppc check``."""
        directory = self.project()
        self.write(directory, "prog.pp", source)
        return self.run_command([PPC, "check", "prog.pp"], cwd=directory,
                                timeout=BUILD_TIMEOUT)

    # -- assertions ---------------------------------------------------------
    def assertOk(self, result, note=""):
        self.assertEqual(
            result.returncode, 0,
            "expected success{}\nstdout={!r}\nstderr={!r}".format(
                " (" + note + ")" if note else "", result.stdout, result.stderr),
        )

    def assertStdout(self, result, expected):
        self.assertOk(result)
        self.assertEqual(result.stdout, expected)

    def assertLines(self, result, lines):
        self.assertOk(result)
        self.assertEqual(result.stdout.splitlines(), lines)

    def assertCheckError(self, result, needle):
        self.assertNotEqual(
            result.returncode, 0,
            "expected a compile error mentioning {!r} but the command "
            "succeeded\nstdout={!r}".format(needle, result.stdout),
        )
        combined = result.stdout + result.stderr
        self.assertIn(needle, combined,
                      "missing {!r} in diagnostics:\n{}".format(needle, combined))

    def assertPanic(self, result, needle):
        self.assertEqual(
            result.returncode, PANIC_STATUS,
            "expected panic exit {} but got {}\nstdout={!r}\nstderr={!r}".format(
                PANIC_STATUS, result.returncode, result.stdout, result.stderr),
        )
        self.assertIn(needle, result.stderr,
                      "missing panic text {!r} in:\n{}".format(needle, result.stderr))

# --------------------------------------------------------------------------
# 1. New syntax, scalars, and entry-point behaviour.
# --------------------------------------------------------------------------
class SyntaxAndBasicsTest(ToolchainTestCase):
    def test_scalar_say_forms(self):
        result = self.build_and_run(
            """
            launch:
                say "text"
                say 42
                say -7
                say 3.5
                say yes
                say no
            done
            """
        )
        self.assertLines(result, ["text", "42", "-7", "3.5", "yes", "no"])

    def test_launch_fallthrough_is_zero(self):
        result = self.build_and_run(
            """
            launch:
                say "done fallthrough"
            done
            """
        )
        self.assertStdout(result, "done fallthrough\n")
        self.assertEqual(result.returncode, 0)

    def test_explicit_exit_status(self):
        result = self.build_and_run(
            """
            launch:
                say "bye"
                give 3
            done
            """
        )
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, "bye\n")

    def test_semicolon_separated_statements(self):
        result = self.build_and_run("""launch: say "a"; say "b"; done\n""")
        self.assertLines(result, ["a", "b"])

    def test_functions_recursion_and_void(self):
        result = self.build_and_run(
            """
            craft classify(n as int) gives str:
                when n > 10:
                    give "large"
                otherwise:
                    give "small"
                done
            done

            craft announce(message as str):
                say message
            done

            craft fib(n as int) gives int:
                when n <= 1:
                    give n
                done
                give fib(n - 1) + fib(n - 2)
            done

            launch:
                announce(classify(42))
                announce(classify(2))
                say fib(10)
            done
            """
        )
        self.assertLines(result, ["large", "small", "55"])

    def test_integer_division_truncates_toward_zero(self):
        result = self.build_and_run(
            """
            launch:
                say 7 / 2
                say -7 / 2
                say 7 % 3
                say -7 % 3
            done
            """
        )
        self.assertLines(result, ["3", "-3", "1", "-1"])

    def test_explicit_conversions(self):
        result = self.build_and_run(
            """
            launch:
                say decimal(7) / decimal(2)
                say whole(3.9)
                say text(-5)
                say parse_int("-123")
            done
            """
        )
        self.assertLines(result, ["3.5", "3", "-5", "-123"])


# --------------------------------------------------------------------------
# 2. Records: value copies, nested copies, mutable fields, pin rejection.
# --------------------------------------------------------------------------
class RecordTest(ToolchainTestCase):
    NESTED = """
    shape Person:
        name as str
        age as int
    done

    shape Team:
        lead as Person
        scores as nums
    done

    launch:
        pin original <- Team(Person("Ada", 42), [10, 20])
        keep updated <- original
        updated.lead.age <- 43
        say original.lead.age
        say updated.lead.age
        updated.scores[0] <- 99
        say original.scores[0]
        updated.scores <- []
        say size(original.scores)
        say size(updated.scores)
    done
    """

    def test_nested_copy_and_shared_list_handle(self):
        result = self.build_and_run(self.NESTED)
        # Nested scalar copy is independent (42 vs 43); the nums handle is shared
        # (99 reaches the original); replacing a handle does not affect aliases.
        self.assertLines(result, ["42", "43", "99", "2", "0"])

    def test_mutable_record_field_assignment(self):
        result = self.build_and_run(
            """
            shape Point:
                x as int
                y as int
            done

            launch:
                keep p <- Point(1, 2)
                p.x <- 10
                p.y <- p.y + 5
                say p.x
                say p.y
            done
            """
        )
        self.assertLines(result, ["10", "7"])

    def test_pin_record_field_assignment_rejected(self):
        result = self.check_source(
            """
            shape Point:
                x as int
                y as int
            done

            launch:
                pin p <- Point(1, 2)
                p.x <- 10
                say p.x
            done
            """
        )
        self.assertCheckError(result, "cannot mutate field through an immutable value")

    def test_record_parameter_is_a_copy(self):
        result = self.build_and_run(
            """
            shape Counter:
                value as int
            done

            craft bump(c as Counter) gives int:
                keep local <- c
                local.value <- local.value + 100
                give local.value
            done

            launch:
                pin c <- Counter(1)
                say bump(c)
                say c.value
            done
            """
        )
        self.assertLines(result, ["101", "1"])


# --------------------------------------------------------------------------
# 3. Integer lists: aliasing, bounds, negative index, pop, sort extremes.
# --------------------------------------------------------------------------
class ListTest(ToolchainTestCase):
    def test_alias_mutation_is_shared(self):
        result = self.build_and_run(
            """
            launch:
                pin xs as nums <- [1, 2, 3]
                pin alias <- xs
                alias[0] <- 42
                push(alias, 4)
                say xs[0]
                say size(xs)
            done
            """
        )
        self.assertLines(result, ["42", "4"])

    def test_pin_list_elements_still_mutable(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [5, 6, 7]
                xs[1] <- 60
                push(xs, 8)
                put(xs, 0, 50)
                say xs[0]
                say xs[1]
                say size(xs)
            done
            """
        )
        self.assertLines(result, ["50", "60", "4"])

    def test_pop_and_size(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [10, 20, 30]
                say pop(xs)
                say size(xs)
                say pop(xs)
                say size(xs)
            done
            """
        )
        self.assertLines(result, ["30", "2", "20", "1"])

    def test_sort_in_place_across_aliases(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [3, 1, 2]
                pin alias <- xs
                sort(xs)
                say alias[0]
                say alias[1]
                say alias[2]
            done
            """
        )
        self.assertLines(result, ["1", "2", "3"])

    def test_sort_extremes_including_min_and_max_int(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [9223372036854775807, -9223372036854775808, 0, -1, 1]
                sort(xs)
                each i from 0 until size(xs):
                    say xs[i]
                done
            done
            """
        )
        self.assertLines(result, [
            "-9223372036854775808", "-1", "0", "1", "9223372036854775807",
        ])

    def test_empty_and_numbers_constructors(self):
        result = self.build_and_run(
            """
            launch:
                pin a <- []
                pin b <- numbers()
                push(a, 1)
                push(b, 2)
                push(b, 3)
                say size(a)
                say size(b)
            done
            """
        )
        self.assertLines(result, ["1", "2"])

    def test_negative_index_panics(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [1, 2, 3]
                say xs[-1]
            done
            """
        )
        self.assertPanic(result, "numbers index out of bounds")

    def test_out_of_bounds_index_panics(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- [1, 2, 3]
                say xs[3]
            done
            """
        )
        self.assertPanic(result, "numbers index out of bounds")

    def test_pop_empty_panics(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- []
                say pop(xs)
            done
            """
        )
        self.assertPanic(result, "pop from empty numbers list")


# --------------------------------------------------------------------------
# 4. Standard library values: math, stats, text.
# --------------------------------------------------------------------------
class StandardLibraryTest(ToolchainTestCase):
    def test_math_values(self):
        result = self.build_and_run(
            """
            bring std.math
            launch:
                say minimum(5, 3)
                say maximum(5, 3)
                say clamp(15, 0, 10)
                say clamp(-4, 0, 10)
                say gcd(48, 36)
                say gcd(-9223372036854775808, 2)
                say factorial(5)
                say integer_power(2, 10)
                say integer_power(7, 0)
            done
            """
        )
        self.assertLines(result, [
            "3", "5", "10", "0", "12", "2", "120", "1024", "1",
        ])

    def test_stats_values_and_nonmutating_sort(self):
        result = self.build_and_run(
            """
            bring std.stats
            launch:
                pin xs <- [4, 2, 9, 1]
                say stats_sum(xs)
                say stats_min(xs)
                say stats_max(xs)
                say stats_mean(xs)
                pin ordered <- stats_sorted(xs)
                say ordered[0]
                say ordered[3]
                say xs[0]
            done
            """
        )
        # stats_mean([4,2,9,1]) = 16/4 = 4.0, printed as "4"; stats_sorted does
        # not mutate its input, so xs[0] remains 4.
        self.assertLines(result, ["16", "1", "9", "4", "1", "9", "4"])

    def test_text_values(self):
        result = self.build_and_run(
            """
            bring std.text
            launch:
                say text_starts_with("hello world", "hello")
                say text_starts_with("hello", "world")
                say text_ends_with("hello world", "world")
                say text_trim("   padded  ")
                say text_repeat("ab", 3)
                say text_repeat("x", 0)
            done
            """
        )
        self.assertLines(result, [
            "yes", "no", "yes", "padded", "ababab", "",
        ])

    def test_nums_time_and_testing_modules(self):
        result = self.build_and_run(
            """
            bring std.nums
            bring std.time
            bring std.testing
            launch:
                pin xs <- [1, 2, 2, 3]
                pin copied <- nums_copy(xs)
                pin reversed <- nums_reverse(xs)
                expect_bool(nums_equal(copied, xs), yes)
                expect_bool(nums_contains(xs, 2), yes)
                expect_int(nums_index_of(xs, 2), 1)
                expect_int(nums_count(xs, 2), 2)
                expect_int(reversed[0], 3)
                pin started <- clock_ms()
                expect_bool(elapsed_ms(started) >= 0, yes)
                expect_str("punpun", "punpun")
                say "extra stdlib ok"
            done
            """
        )
        self.assertStdout(result, "extra stdlib ok\n")


# --------------------------------------------------------------------------
# 5. Control flow: each semantics, next/leave, immutable binding, whilst.
# --------------------------------------------------------------------------
class ControlFlowTest(ToolchainTestCase):
    def test_each_exclusive_equal_and_descending(self):
        result = self.build_and_run(
            """
            launch:
                keep exclusive <- 0
                each i from 0 until 3:
                    exclusive <- exclusive + 1
                done
                keep equal <- 0
                each i from 5 until 5:
                    equal <- equal + 1
                done
                keep descending <- 0
                each i from 5 until 2:
                    descending <- descending + 1
                done
                say exclusive
                say equal
                say descending
            done
            """
        )
        # Exclusive end -> 3 iterations; equal and descending bounds -> zero.
        self.assertLines(result, ["3", "0", "0"])

    def test_each_next_and_leave(self):
        result = self.build_and_run(
            """
            launch:
                each i from 0 until 6:
                    when i % 2 == 0:
                        next
                    done
                    say i
                done
                each i from 0 until 100:
                    when i == 3:
                        leave
                    done
                    say i
                done
            done
            """
        )
        self.assertLines(result, ["1", "3", "5", "0", "1", "2"])

    def test_range_binding_is_immutable(self):
        result = self.check_source(
            """
            launch:
                each i from 0 until 5:
                    i <- i + 1
                done
            done
            """
        )
        self.assertCheckError(result, "cannot assign to immutable binding 'i'")

    def test_range_bounds_evaluate_once(self):
        result = self.build_and_run(
            """
            craft note(log as nums, value as int) gives int:
                push(log, value)
                give value
            done

            launch:
                pin log <- numbers()
                keep iterations <- 0
                each i from note(log, 0) until note(log, 3):
                    iterations <- iterations + 1
                done
                say iterations
                say size(log)
                say log[0]
                say log[1]
            done
            """
        )
        # Both bounds are evaluated exactly once, in order, before iterating.
        self.assertLines(result, ["3", "2", "0", "3"])

    def test_whilst_reevaluates_condition(self):
        result = self.build_and_run(
            """
            launch:
                pin stack <- [10, 20, 30]
                whilst size(stack) > 0:
                    say pop(stack)
                done
                keep remaining <- 3
                whilst remaining > 0:
                    say remaining
                    remaining <- remaining - 1
                done
            done
            """
        )
        self.assertLines(result, ["30", "20", "10", "3", "2", "1"])


# --------------------------------------------------------------------------
# 6. Evaluation order and short-circuit semantics.
# --------------------------------------------------------------------------
class EvaluationOrderTest(ToolchainTestCase):
    def test_left_to_right_argument_evaluation(self):
        result = self.build_and_run(
            """
            craft note(log as nums, tag as int) gives int:
                push(log, tag)
                give tag
            done

            launch:
                pin log <- numbers()
                pin total <- note(log, 1) + note(log, 2) * note(log, 3)
                say total
                each i from 0 until size(log):
                    say log[i]
                done
            done
            """
        )
        # Operands evaluate left-to-right regardless of precedence: log is
        # [1, 2, 3]; the value still respects precedence: 1 + 2 * 3 = 7.
        self.assertLines(result, ["7", "1", "2", "3"])

    def test_indexed_assignment_evaluation_order(self):
        result = self.build_and_run(
            """
            craft note(log as nums, tag as int) gives int:
                push(log, tag)
                give tag
            done

            launch:
                pin log <- numbers()
                pin xs <- [0, 0, 0, 0]
                xs[note(log, 1)] <- note(log, 2)
                say log[0]
                say log[1]
                say xs[1]
            done
            """
        )
        # Index expression is evaluated before the assigned value.
        self.assertLines(result, ["1", "2", "2"])

    def test_and_short_circuit_protects_out_of_bounds(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- []
                when size(xs) > 0 and xs[0] == 42:
                    say "found"
                otherwise:
                    say "safe"
                done
            done
            """
        )
        self.assertStdout(result, "safe\n")

    def test_or_short_circuit_protects_out_of_bounds(self):
        result = self.build_and_run(
            """
            launch:
                pin xs <- []
                when size(xs) == 0 or xs[0] == 42:
                    say "safe"
                done
            done
            """
        )
        self.assertStdout(result, "safe\n")

    def test_short_circuit_side_effects(self):
        result = self.build_and_run(
            """
            craft touched(log as nums, value as int) gives bool:
                push(log, value)
                give yes
            done

            launch:
                pin log <- numbers()
                when no and touched(log, 1):
                    say "unreachable"
                done
                when yes or touched(log, 2):
                    say "reached"
                done
                say size(log)
            done
            """
        )
        # Neither guarded call runs: the right operand is skipped in both cases.
        self.assertLines(result, ["reached", "0"])


# --------------------------------------------------------------------------
# 7. Type-checker / frontend diagnostics.
# --------------------------------------------------------------------------
class TypeCheckerErrorTest(ToolchainTestCase):
    def test_unknown_type(self):
        result = self.check_source(
            """
            craft f(x as Widget) gives int:
                give 0
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "unknown or invalid value type 'Widget'")

    def test_unknown_field(self):
        result = self.check_source(
            """
            shape P:
                name as str
            done
            launch:
                pin p <- P("a")
                say p.age
            done
            """
        )
        self.assertCheckError(result, "type 'P' has no field 'age'")

    def test_no_guaranteed_return(self):
        result = self.check_source(
            """
            craft incomplete(value as bool) gives int:
                when value:
                    give 1
                done
            done
            launch:
                say incomplete(yes)
            done
            """
        )
        self.assertCheckError(result, "may exit without returning int")

    def test_return_only_in_loop_is_insufficient(self):
        result = self.check_source(
            """
            craft spin() gives int:
                whilst yes:
                    give 1
                done
            done
            launch:
                say spin()
            done
            """
        )
        self.assertCheckError(result, "may exit without returning int")

    def test_void_initializer(self):
        result = self.check_source(
            """
            craft noop():
                say "x"
            done
            launch:
                pin v <- noop()
                say 0
            done
            """
        )
        self.assertCheckError(result, "unknown or invalid value type 'void'")

    def test_void_parameter(self):
        result = self.check_source(
            """
            craft f(x as void) gives int:
                give 0
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "parameter cannot have type void")

    def test_arity_mismatch(self):
        result = self.check_source(
            """
            craft f(x as int) gives int:
                give x
            done
            launch:
                say f(1, 2)
            done
            """
        )
        self.assertCheckError(result, "expects 1 argument(s), got 2")

    def test_reserved_word_as_function_name(self):
        result = self.check_source(
            """
            craft when(x as int) gives int:
                give x
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "reserved word 'when' cannot be a name")

    def test_reserved_word_as_binding_name(self):
        result = self.check_source(
            """
            launch:
                pin done <- 5
                say done
            done
            """
        )
        self.assertCheckError(result, "reserved word 'done' cannot be a name")

    def test_duplicate_function_declaration(self):
        result = self.check_source(
            """
            craft f(x as int) gives int:
                give x
            done
            craft f(y as int) gives int:
                give y
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "duplicate function 'f'")

    def test_recursive_shape_rejected(self):
        result = self.check_source(
            """
            shape Node:
                value as int
                child as Node
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "recursive by-value struct 'Node'")

    def test_mutually_recursive_shapes_rejected(self):
        result = self.check_source(
            """
            shape A:
                b as B
            done
            shape B:
                a as A
            done
            launch:
                say 0
            done
            """
        )
        self.assertCheckError(result, "recursive by-value struct")

    def test_bad_syntax_missing_done(self):
        result = self.check_source(
            """
            launch:
                say "hello"
            """
        )
        self.assertCheckError(result, "expected 'done' after block")

    def test_integer_literal_out_of_range(self):
        result = self.check_source(
            """
            launch:
                keep a <- 9223372036854775808
                say a
            done
            """
        )
        self.assertCheckError(result, "outside signed 64-bit range")

    def test_min_int_literal_is_valid(self):
        result = self.build_and_run(
            """
            launch:
                keep a <- -9223372036854775808
                say a
            done
            """
        )
        self.assertStdout(result, "-9223372036854775808\n")

    def test_leading_zero_integers_are_decimal(self):
        # Leading zeros are accepted and interpreted as decimal (not octal and
        # not rejected): 007 == 7, 010 == 10, 09 == 9.
        result = self.build_and_run(
            """
            launch:
                say 007
                say 010
                say 09
            done
            """
        )
        self.assertLines(result, ["7", "10", "9"])

    def test_immutable_pin_reassignment(self):
        result = self.check_source(
            """
            launch:
                pin x <- 5
                x <- 6
                say x
            done
            """
        )
        self.assertCheckError(result, "cannot assign to immutable binding 'x'")

    def test_mixed_numeric_arithmetic(self):
        result = self.check_source(
            """
            launch:
                say 1 + 2.0
            done
            """
        )
        self.assertCheckError(result, "operator '+' received int and float")

    def test_return_type_mismatch_float(self):
        result = self.check_source(
            """
            craft half() gives float:
                give 1
            done
            launch:
                say half()
            done
            """
        )
        self.assertCheckError(result, "return requires float, got int")

    def test_return_type_mismatch_int(self):
        result = self.check_source(
            """
            craft f() gives int:
                give 1.5
            done
            launch:
                say f()
            done
            """
        )
        self.assertCheckError(result, "return requires int, got float")

    def test_unknown_function(self):
        result = self.check_source(
            """
            launch:
                say mystery(5)
            done
            """
        )
        self.assertCheckError(result, "unknown function 'mystery'")

    def test_string_condition_rejected(self):
        result = self.check_source(
            """
            launch:
                when "yes":
                    say "no truthiness"
                done
            done
            """
        )
        self.assertCheckError(result, "if condition requires bool, got str")


# --------------------------------------------------------------------------
# 8. Rejecting 0.1 (Rust-like) syntax.
# --------------------------------------------------------------------------
class ModernSyntaxTest(ToolchainTestCase):
    def test_fn_and_braces_are_accepted(self):
        result = self.check_source(
            """
            fn main() -> i64 {
                let x: i64 = 5;
                return x - 5;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_import_keyword_is_accepted_and_use_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="punpun-modern-import-") as directory:
            with open(os.path.join(directory, "math.pp"), "w", encoding="utf-8") as handle:
                handle.write('fn value() -> i64 { return 1; }\n')
            with open(os.path.join(directory, "prog.pp"), "w", encoding="utf-8") as handle:
                handle.write('import math;\nfn main() { println(value()); }\n')
            good = subprocess.run([PPC, "check", "prog.pp"], cwd=directory, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            self.assertEqual(good.returncode, 0, good.stderr)
            with open(os.path.join(directory, "prog.pp"), "w", encoding="utf-8") as handle:
                handle.write('use math;\nfn main() {}\n')
            bad = subprocess.run([PPC, "check", "prog.pp"], cwd=directory, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("expected declaration", bad.stderr)

    def test_modern_logical_operators_are_accepted(self):
        result = self.check_source(
            """
            fn main() {
                println((1 == 1) && (2 == 2));
                println((1 == 2) || (2 == 2));
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_return_keyword_is_accepted(self):
        result = self.check_source(
            """
            fn f() -> i64 {
                return 1;
            }
            fn main() {
                println(f());
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stderr)


# --------------------------------------------------------------------------
# 9. Arguments, exit-status propagation, and file I/O.
# --------------------------------------------------------------------------
class ArgumentsAndIoTest(ToolchainTestCase):
    ARG_ECHO = """
    launch:
        say arg_count()
        each i from 0 until arg_count():
            say arg(i)
        done
    done
    """

    def test_arguments_with_spaces_and_metacharacters(self):
        tricky = ["hello world", "a$b`c;d", 'quote"inside', "tab\tsep", "*?[]"]
        result = self.build_and_run(self.ARG_ECHO, args=tricky)
        self.assertLines(result, ["5", *tricky])

    def test_nonzero_exit_propagates_through_ppc_go(self):
        result = self.build_and_run(
            """
            launch:
                say "failing"
                give 7
            done
            """
        )
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout, "failing\n")

    def test_argument_out_of_range_panics(self):
        result = self.build_and_run(
            """
            launch:
                say arg(0)
            done
            """
        )
        self.assertPanic(result, "argument index out of bounds")

    def test_file_io_roundtrip(self):
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   launch:
                       pin path <- arg(0)
                       write_text(path, "roundtrip\\nsecond line\\n")
                       pin back <- read_text(path)
                       print(back)
                       say len(back)
                   done
                   """)
        target = os.path.join(directory, "data.txt")
        result = self.run_command(
            [PPC, "go", "prog.pp", "--", target], cwd=directory,
            timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertEqual(result.stdout, "roundtrip\nsecond line\n22\n")
        with open(target, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "roundtrip\nsecond line\n")

    def test_read_missing_file_panics(self):
        result = self.build_and_run(
            """
            launch:
                say read_text("definitely-not-here.txt")
            done
            """
        )
        self.assertPanic(result, "cannot open text file for reading")


# --------------------------------------------------------------------------
# 10. Runtime panics: checked arithmetic and library preconditions.
# --------------------------------------------------------------------------
class PanicTest(ToolchainTestCase):
    def test_integer_overflow_panics(self):
        result = self.build_and_run(
            """
            launch:
                keep a <- 9223372036854775807
                say a + 1
            done
            """
        )
        self.assertPanic(result, "integer error in addition")

    def test_division_by_zero_panics(self):
        result = self.build_and_run(
            """
            launch:
                keep a <- 10
                keep b <- 0
                say a / b
            done
            """
        )
        self.assertPanic(result, "integer error in division")

    def test_remainder_by_zero_panics(self):
        result = self.build_and_run(
            """
            launch:
                keep a <- 10
                keep b <- 0
                say a % b
            done
            """
        )
        self.assertPanic(result, "integer error in remainder")

    def test_min_int_negation_panics(self):
        result = self.build_and_run(
            """
            launch:
                keep a <- -9223372036854775808
                say -a
            done
            """
        )
        self.assertPanic(result, "integer error in negation")

    def test_parse_int_invalid_panics(self):
        result = self.build_and_run(
            """
            launch:
                say parse_int("12x")
            done
            """
        )
        self.assertPanic(result, "invalid integer text")

    def test_assert_failure_panics(self):
        result = self.build_and_run(
            """
            launch:
                assert(1 == 2, "one is not two")
            done
            """
        )
        self.assertEqual(result.returncode, PANIC_STATUS)
        self.assertIn("one is not two", result.stderr)

    def test_explicit_panic(self):
        result = self.build_and_run(
            """
            launch:
                panic("halt and catch fire")
            done
            """
        )
        self.assertPanic(result, "halt and catch fire")


# --------------------------------------------------------------------------
# 11. CLI: release/debug parity, emit-c, unknown options, no side effects.
# --------------------------------------------------------------------------
class CompilerCliTest(ToolchainTestCase):
    PROGRAM = """
    bring std.stats
    launch:
        pin xs <- [3, 1, 2, 8, 5]
        sort(xs)
        each i from 0 until size(xs):
            say xs[i]
        done
        say stats_sum(xs)
        say stats_mean(xs)
    done
    """

    def test_release_and_default_stdout_match(self):
        directory = self.project()
        self.write(directory, "prog.pp", self.PROGRAM)
        default = self.run_command([PPC, "go", "prog.pp"], cwd=directory,
                                   timeout=BUILD_TIMEOUT)
        release = self.run_command([PPC, "go", "prog.pp", "--release"],
                                   cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertOk(default)
        self.assertOk(release)
        self.assertEqual(default.stdout, release.stdout)

    def test_check_reports_success(self):
        directory = self.project()
        self.write(directory, "prog.pp", self.PROGRAM)
        result = self.run_command([PPC, "check", "prog.pp"], cwd=directory,
                                  timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertIn("checked", result.stdout)
        # check performs no native compilation, hence no binary artifact.
        self.assertFalse(os.path.exists(os.path.join(directory, ".punpun")))

    def test_emit_c_to_stdout(self):
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   launch:
                       say 21 * 2
                   done
                   """)
        result = self.run_command([PPC, "emit-c", "prog.pp"], cwd=directory,
                                  timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertIn("#include \"punpun.h\"", result.stdout)
        self.assertIn("int main(", result.stdout)

    def test_emit_asm_is_real_x86_64_output(self):
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   launch:
                       say 40 + 2
                   done
                   """)
        result = self.run_command([PPC, "emit-asm", "prog.pp"], cwd=directory,
                                  timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertIn(".intel_syntax noprefix", result.stdout)
        self.assertIn("pp_fn_main:", result.stdout)
        self.assertIn("call pp_add_i64@PLT", result.stdout)

    def test_emit_c_compile_parity(self):
        compiler = shutil.which(os.environ.get("CC", "cc"))
        if compiler is None:
            self.skipTest("no C compiler available for parity check")
        if not os.path.isfile(LIBPUNPUN):
            self.skipTest("runtime static library is not built")
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   bring std.stats
                   shape Box:
                       lo as int
                       hi as int
                   done
                   launch:
                       pin b <- Box(3, 9)
                       pin xs <- [b.hi, b.lo, 5, 1]
                       sort(xs)
                       each i from 0 until size(xs):
                           say xs[i]
                       done
                       say stats_sum(xs)
                   done
                   """)
        native = self.run_command([PPC, "go", "prog.pp"], cwd=directory,
                                  timeout=BUILD_TIMEOUT)
        self.assertOk(native)
        emit = self.run_command([PPC, "emit-c", "prog.pp", "-o", "prog.c"],
                                cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertOk(emit)
        binary = os.path.join(directory, "prog_from_c")
        compile_result = self.run_command(
            [compiler, "-std=c17", "-O0", "-g", "-I", RUNTIME,
             os.path.join(directory, "prog.c"), LIBPUNPUN,
             "-lm", "-o", binary],
            cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertOk(compile_result, "cc failed on emitted C")
        c_run = self.run_command([binary], cwd=directory, timeout=RUN_TIMEOUT)
        self.assertOk(c_run)
        self.assertEqual(c_run.stdout, native.stdout)

    def test_unknown_option_errors_without_side_effects(self):
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   launch:
                       say "hi"
                   done
                   """)
        result = self.run_command([PPC, "go", "prog.pp", "--bogus"],
                                  cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown option '--bogus'", result.stderr)
        # The bad option is rejected before any build artifact is produced.
        self.assertFalse(os.path.exists(os.path.join(directory, ".punpun")))

    def test_output_must_not_overwrite_source(self):
        directory = self.project()
        self.write(directory, "prog.pp",
                   """
                   launch:
                       say "hi"
                   done
                   """)
        result = self.run_command([PPC, "build", "prog.pp", "-o", "prog.pp"],
                                  cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output must not overwrite Punpun source", result.stderr)


# --------------------------------------------------------------------------
# 12. Bundled examples: hello, showcase, sieve.
# --------------------------------------------------------------------------
class ExamplesTest(ToolchainTestCase):
    def _run_example(self, relative, args=(), release=False):
        directory = self.project()
        source = os.path.join(EXAMPLES, relative)
        self.assertTrue(os.path.isfile(source), "missing example: " + source)
        argv = [PPC, "go", source]
        if release:
            argv.append("--release")
        if args:
            argv.append("--")
            argv.extend(str(a) for a in args)
        return self.run_command(argv, cwd=directory, timeout=BUILD_TIMEOUT)

    def test_hello_example(self):
        result = self._run_example("hello/main.pp")
        self.assertLines(result, [
            "Punpun",
            "fibonacci(10) = 55",
            "sum_to(100) = 5050",
            "safe abs(-42) = 42",
            "name length = 6",
        ])

    def test_showcase_example(self):
        result = self._run_example("showcase.pp")
        self.assertOk(result)
        lines = result.stdout.splitlines()
        self.assertIn("PUNPUN / SENSOR REPORT", lines)
        self.assertIn("Samples: 7", lines)
        self.assertIn("Budget: 40 ms", lines)
        self.assertIn("Range: 17..61 ms", lines)
        self.assertIn("1 sample(s) exceed the budget.", lines)
        # Sorted samples appear in order.
        self.assertEqual(
            [l for l in lines if l in {"17", "19", "22", "24", "28", "35", "61"}],
            ["17", "19", "22", "24", "28", "35", "61"],
        )

    def test_showcase_release_matches_default(self):
        default = self._run_example("showcase.pp")
        release = self._run_example("showcase.pp", release=True)
        self.assertOk(default)
        self.assertOk(release)
        self.assertEqual(default.stdout, release.stdout)

    def test_showcase_reads_notes_file(self):
        directory = self.project()
        notes = os.path.join(directory, "notes.txt")
        with open(notes, "w", encoding="utf-8") as handle:
            handle.write("   trimmed body   \n")
        result = self.run_command(
            [PPC, "go", os.path.join(EXAMPLES, "showcase.pp"), "--", notes],
            cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertIn("Note: trimmed body", result.stdout.splitlines())

    def test_sieve_small_input(self):
        result = self._run_example("algorithms/sieve.pp", args=[100, 1])
        self.assertOk(result)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "Primes <= 100: 25")
        self.assertEqual(lines[1], "Rounds: 1, checksum: 25")

    def test_sieve_larger_prime_count(self):
        result = self._run_example("algorithms/sieve.pp", args=[1000, 2])
        self.assertOk(result)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "Primes <= 1000: 168")
        self.assertEqual(lines[1], "Rounds: 2, checksum: 336")


# --------------------------------------------------------------------------
# 0.5 native OOP contracts and explicit foreign-language injection.
# --------------------------------------------------------------------------
class PunPun05VerticalSliceTest(ToolchainTestCase):
    def test_named_and_default_arguments_on_functions_constructors_and_methods(self):
        source = """
            fn decorate(value: String, prefix: String = "[", suffix: String = "]") -> String {
                return prefix + value + suffix;
            }
            object Scale {
                private let value: i64;
                private let factor: i64;
                public init(value: i64, factor: i64 = 2) {
                    self.value = value;
                    self.factor = factor;
                }
                public fn apply(extra: i64 = 0) -> i64 {
                    return self.value * self.factor + extra;
                }
            }
            launch {
                say(decorate("PunPun"));
                say(decorate(value: "named", suffix: "!", prefix: "<"));
                let first = Scale(21);
                let second = Scale(factor: 3, value: 10);
                say(first.apply());
                say(second.apply(extra: 12));
            }
        """
        direct = self.build_and_run(source)
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertLines(direct, ["[PunPun]", "<named!", "42", "42"])
        self.assertLines(portable, direct.stdout.splitlines())

    def test_named_argument_diagnostics(self):
        unknown = self.check_source("""
            fn value(left: i64, right: i64 = 1) -> i64 { return left + right; }
            launch { say(value(wrong: 2)); }
        """)
        self.assertCheckError(unknown, "E1201")
        duplicate = self.check_source("""
            fn value(left: i64, right: i64 = 1) -> i64 { return left + right; }
            launch { say(value(2, left: 3)); }
        """)
        self.assertCheckError(duplicate, "E1202")

    OWNERSHIP_PROGRAM = """
        object Box {
            private let value: i64;
            public init(value: i64) { self.value = value; }
            public fn get() -> i64 { return self.value; }
        }
        launch {
            let first = Box(42);
            let second = move(first);
            say(second.get());
            drop(second);

            let mut values = numbers();
            push(values, 7);
            let transferred = move(values);
            say(at(transferred, 0));
            drop(transferred);
        }
    """

    def test_explicit_move_and_drop_direct_and_c_backends(self):
        direct = self.build_and_run(self.OWNERSHIP_PROGRAM)
        portable = self.build_and_run(self.OWNERSHIP_PROGRAM, extra=["--cc-backend"])
        self.assertStdout(direct, "42\n7\n")
        self.assertStdout(portable, direct.stdout)

    def test_use_after_move_is_rejected(self):
        result = self.check_source("""
            object Box { public init() {} }
            launch {
                let first = Box();
                let second = move(first);
                drop(second);
                drop(first);
            }
        """)
        self.assertCheckError(result, "E0703")
        self.assertCheckError(result, "use of moved value 'first'")

    def test_moved_mutable_binding_can_be_reinitialized(self):
        result = self.build_and_run("""
            object Box {
                private let value: i64;
                public init(value: i64) { self.value = value; }
                public fn get() -> i64 { return self.value; }
            }
            launch {
                let mut first = Box(1);
                let old = move(first);
                first = Box(9);
                say(first.get());
                drop(old);
                drop(first);
            }
        """)
        self.assertStdout(result, "9\n")

    def test_distinct_launch_bring_and_say_syntax(self):
        directory = self.project()
        self.write(directory, "helper.pp", "fn value() -> i64 { return 42; }\n")
        self.write(directory, "prog.pp", """
            bring helper;
            launch {
                say(value());
            }
        """)
        result = self.run_command([PPC, "go", "prog.pp"], cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertStdout(result, "42\n")

    def test_contract_and_implicit_self_object_methods(self):
        result = self.build_and_run("""
            contract Damageable {
                fn damage(amount: i64);
            }

            object Enemy meets Damageable {
                private let mut health: i64;

                public init(health: i64) {
                    self.health = health;
                }

                public fn damage(amount: i64) {
                    self.health -= amount;
                }

                public fn hp() -> i64 {
                    return self.health;
                }
            }

            launch {
                let mut enemy = Enemy(100);
                enemy.damage(25);
                say(enemy.hp());
            }
        """)
        self.assertStdout(result, "75\n")

    def test_missing_contract_method_is_a_real_error(self):
        result = self.check_source("""
            contract Drawable {
                fn draw();
            }
            object Player meets Drawable {
                public init() {}
            }
            launch {}
        """)
        self.assertCheckError(result, "E0403")
        self.assertCheckError(result, "does not implement contract method")

    def test_unknown_name_has_spelling_suggestion(self):
        result = self.check_source("""
            launch {
                let player = 1;
                say(playre);
            }
        """)
        self.assertCheckError(result, "E0201")
        self.assertCheckError(result, "did you mean `player`?")

    @unittest.skipUnless(shutil.which("cc"), "a C compiler is required for @inject->c integration")
    def test_c_injection_links_through_direct_and_c_backends(self):
        source = '@inject->c("""\n#include <stdint.h>\nint64_t fast_native_add(int64_t a, int64_t b) {\n    return a + b;\n}\n""");\nextern native fn fast_native_add(a: i64, b: i64) -> i64;\nlaunch {\n    say(fast_native_add(10, 20));\n}\n'
        direct = self.build_and_run(source)
        self.assertStdout(direct, "30\n")
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertStdout(portable, "30\n")

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++") or shutil.which("clang++"),
                         "a C++ compiler is required for @inject->cpp integration")
    def test_cpp_injection_links_through_direct_and_c_backends(self):
        source = '@inject->cpp("""\n#include <cstdint>\nextern "C" std::int64_t cpp_add(std::int64_t a, std::int64_t b) { return a + b; }\n""");\nextern native fn cpp_add(a: i64, b: i64) -> i64;\nlaunch { say(cpp_add(20, 22)); }\n'
        direct = self.build_and_run(source)
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertStdout(direct, "42\n")
        self.assertStdout(portable, direct.stdout)

    @unittest.skipUnless(shutil.which("cc"), "a compiler driver is required for @inject->asm integration")
    def test_assembly_injection_links_through_direct_and_c_backends(self):
        source = '@inject->asm("""\n.text\n.globl asm_add\n.type asm_add,@function\nasm_add:\n    lea (%rdi,%rsi), %rax\n    ret\n.section .note.GNU-stack,"",@progbits\n""");\nextern native fn asm_add(a: i64, b: i64) -> i64;\nlaunch { say(asm_add(40, 2)); }\n'
        direct = self.build_and_run(source)
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertStdout(direct, "42\n")
        self.assertStdout(portable, direct.stdout)

    @unittest.skipUnless(shutil.which("rustc"), "rustc is required for @inject->rust integration")
    def test_rust_injection_links_through_direct_and_c_backends(self):
        source = '@inject->rust("""\n#[no_mangle]\npub extern "C" fn rust_add(a: i64, b: i64) -> i64 { a + b }\n""");\nextern native fn rust_add(a: i64, b: i64) -> i64;\nlaunch { say(rust_add(40, 2)); }\n'
        direct = self.build_and_run(source)
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertStdout(direct, "42\n")
        self.assertStdout(portable, direct.stdout)

    @unittest.skipUnless(shutil.which("cc"), "a C compiler is required for @inject->c integration")
    def test_injection_check_is_safe_and_foreign_object_is_cached(self):
        directory = self.project()
        source = '@inject->c("""\n#include <stdint.h>\nint64_t native_identity(int64_t x) { return x; }\n""");\nextern native fn native_identity(x: i64) -> i64;\nlaunch { say(native_identity(7)); }\n'
        self.write(directory, "prog.pp", source)
        checked = self.ppc("check", "prog.pp", cwd=directory)
        self.assertOk(checked)
        first = self.ppc("build", "prog.pp", "-o", "one", cwd=directory)
        self.assertOk(first)
        objects = list(Path(directory, ".punpun", "cache", "inject").glob("*.o"))
        self.assertEqual(len(objects), 1)
        stamp = objects[0].stat().st_mtime_ns
        second = self.ppc("build", "prog.pp", "-o", "two", cwd=directory)
        self.assertOk(second)
        self.assertEqual(objects[0].stat().st_mtime_ns, stamp, "unchanged C injection was recompiled")

    @unittest.skipUnless(shutil.which("cc"), "a C compiler is required for @inject->c integration")
    def test_invalid_c_is_not_executed_by_check_but_fails_build(self):
        directory = self.project()
        source = '@inject->c("""\n#include <stdint.h>\nint64_t broken(int64_t x) { return x + ; }\n""");\nextern native fn broken(x: i64) -> i64;\nlaunch { say(broken(1)); }\n'
        self.write(directory, "prog.pp", source)
        checked = self.ppc("check", "prog.pp", cwd=directory)
        self.assertOk(checked)
        built = self.ppc("build", "prog.pp", cwd=directory)
        self.assertNotEqual(built.returncode, 0)
        self.assertIn("E0603", built.stderr)
        self.assertIn("prog.pp", built.stderr)


# --------------------------------------------------------------------------
# Environment sanity: fail fast with a clear message if the toolchain is absent.
# --------------------------------------------------------------------------
class ToolchainPresenceTest(unittest.TestCase):
    def test_compiler_exists(self):
        self.assertTrue(
            os.path.isfile(PPC) and os.access(PPC, os.X_OK),
            "compiler not found at {}; run `make` first".format(PPC))


def _require_toolchain():
    missing = [path for path in (PPC,) if not os.path.isfile(path)]
    if missing:
        sys.stderr.write(
            "error: missing toolchain binaries: {}\n"
            "build the compiler first (for example, `make compiler`).\n".format(
                ", ".join(missing)))
        return False
    return True


class AsyncRuntimeTest(ToolchainTestCase):
    def test_cooperative_cancellation_and_completion_state(self):
        source = """
            async fn cancellable() -> i64 {
                while not cancelled() {
                    sleep_ms(1);
                }
                return 99;
            }

            launch {
                let task = cancellable();
                say(task_done(task));
                cancel(task);
                say(await task);
                say(task_done(task));
            }
        """
        direct = self.build_and_run(source)
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertLines(direct, ["no", "99", "yes"])
        self.assertLines(portable, ["no", "99", "yes"])

    def test_cancel_requires_task(self):
        result = self.check_source("""
            launch { cancel(42); }
        """)
        self.assertCheckError(result, "E1505")

    def test_async_tasks_execute_concurrently_direct_and_c_backends(self):
        source = """
            async fn delayed(value: i64, delay: i64) -> i64 {
                sleep_ms(delay);
                return value;
            }

            launch {
                let start = clock_ms();
                let task = delayed(42, 400);
                let spawn_elapsed = clock_ms() - start;
                say(spawn_elapsed < 250);
                say(await task);
            }
        """
        direct = self.build_and_run(source)
        self.assertLines(direct, ["yes", "42"])
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertLines(portable, ["yes", "42"])

    def test_async_result_types_and_void_await(self):
        source = """
            async fn decimal_task() -> f64 {
                return 3.5;
            }

            async fn text_task() -> String {
                return "async-ok";
            }

            async fn void_task() {
                sleep_ms(5);
            }

            launch {
                let a = decimal_task();
                let b = text_task();
                let c = void_task();
                say(await a);
                say(await b);
                await c;
                say("done");
            }
        """
        direct = self.build_and_run(source)
        self.assertLines(direct, ["3.5", "async-ok", "done"])
        portable = self.build_and_run(source, extra=["--cc-backend"])
        self.assertLines(portable, ["3.5", "async-ok", "done"])

    def test_await_requires_async_context_or_launch(self):
        result = self.check_source("""
            async fn worker() -> i64 { return 1; }
            fn invalid() -> i64 {
                return await worker();
            }
            launch { say(0); }
        """)
        self.assertCheckError(result, "E1503")

    def test_async_borrowed_parameters_are_rejected(self):
        result = self.check_source("""
            async fn invalid(value: &i64) -> i64 { return *value; }
            launch { say(0); }
        """)
        self.assertCheckError(result, "E1502")


if __name__ == "__main__":
    if not _require_toolchain():
        sys.exit(2)
    unittest.main(verbosity=2)
