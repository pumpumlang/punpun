#!/usr/bin/env python3
"""ppc regression test runner.

Each test is a `.pp` file under tests/cases with a companion expectation:

    NAME.pp + NAME.out   compile and run; stdout must match NAME.out exactly
    NAME.pp + NAME.err   compilation must fail; every diagnostic code listed in
                         NAME.err (one per line, e.g. E0800) must be reported,
                         and no code outside the list may be reported
    NAME.skip            optional; backend names, one per line, that cannot run
                         this case. A skipped case still has to fail cleanly
                         with a diagnostic rather than crash or produce wrong
                         output, and that is checked.

Run pipelines are exercised against every backend the build supports, so a
backend that silently diverges from the C reference output fails here rather
than in a user's program.

Usage:
    tests/run_tests.py [--ppc PATH] [--backend NAME ...] [--filter SUBSTRING]
"""

import argparse
import os
import re
import subprocess
import sys
import time

COMPILER_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(COMPILER_ROOT)
STDLIB = os.path.join(ROOT, "stdlib")
CASES = os.path.join(COMPILER_ROOT, "tests", "cases")

GREEN = "\033[32m"
RED = "\033[31;1m"
YELLOW = "\033[33m"
DIM = "\033[2m"
RESET = "\033[0m"

CODE_PATTERN = re.compile(r"\b(E\d{4})\b")


def colorize(text, color, enabled):
    return f"{color}{text}{RESET}" if enabled else text


class Result:
    def __init__(self, name, backend, ok, detail="", seconds=0.0, skipped=False):
        self.name = name
        self.backend = backend
        self.ok = ok
        self.detail = detail
        self.seconds = seconds
        self.skipped = skipped


def run_case(ppc, source, backend, timeout, opt=None):
    """Runs one case against one backend and returns a Result."""
    name = os.path.basename(source)[:-3]
    expected_out = source[:-3] + ".out"
    expected_err = source[:-3] + ".err"
    skip_file = source[:-3] + ".skip"
    started = time.time()

    if os.path.exists(skip_file):
        unsupported = [line.strip() for line in open(skip_file)
                       if line.strip() and not line.startswith("#")]
        if backend in unsupported:
            # An unsupported case must still be refused cleanly. Silently
            # producing wrong output would be far worse than not compiling.
            proc = subprocess.run(
                [ppc, "build", "--no-color", f"--backend={backend}",
                 "--stdlib", STDLIB]
                + ([f"-O{opt}"] if opt else []) +
                ["-o", os.devnull, source],
                capture_output=True, text=True, timeout=timeout)
            elapsed = time.time() - started
            if proc.returncode == 0:
                return Result(name, backend, False,
                              f"{backend} claims not to support this, but compiled it anyway",
                              elapsed)
            if not CODE_PATTERN.search(proc.stderr):
                return Result(name, backend, False,
                              f"{backend} refused this without a diagnostic code", elapsed)
            return Result(name, backend, True, seconds=elapsed, skipped=True)

    if os.path.exists(expected_err):
        # Error cases exercise the front end only, so they run once regardless
        # of backend.
        if backend != "c":
            return Result(name, backend, True, skipped=True)

        proc = subprocess.run(
            [ppc, "check", "--no-color", "--stdlib", STDLIB, source],
            capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - started

        if proc.returncode == 0:
            return Result(name, backend, False,
                          "expected compilation to fail, but it succeeded", elapsed)

        wanted = [line.strip() for line in open(expected_err)
                  if line.strip() and not line.startswith("#")]
        reported = CODE_PATTERN.findall(proc.stderr)

        missing = [code for code in wanted if code not in reported]
        if missing:
            return Result(name, backend, False,
                          f"expected {', '.join(missing)}; got {', '.join(reported) or 'none'}",
                          elapsed)

        unexpected = sorted({c for c in reported if c not in wanted})
        if unexpected:
            return Result(name, backend, False,
                          f"unexpected diagnostics: {', '.join(unexpected)}", elapsed)

        return Result(name, backend, True, seconds=elapsed)

    if not os.path.exists(expected_out):
        return Result(name, backend, False, "no .out or .err expectation file")

    proc = subprocess.run(
        [ppc, "run", "--no-color", f"--backend={backend}", "--stdlib", STDLIB]
        + ([f"-O{opt}"] if opt else []) + [source],
        capture_output=True, text=True, timeout=timeout)
    elapsed = time.time() - started

    if proc.returncode != 0:
        first = (proc.stderr.strip().splitlines() or ["(no output)"])[0]
        return Result(name, backend, False,
                      f"exited {proc.returncode}: {first}", elapsed)

    wanted = open(expected_out).read()
    if proc.stdout != wanted:
        detail = ["output mismatch"]
        want_lines = wanted.splitlines()
        got_lines = proc.stdout.splitlines()
        for i in range(max(len(want_lines), len(got_lines))):
            w = want_lines[i] if i < len(want_lines) else "(missing)"
            g = got_lines[i] if i < len(got_lines) else "(missing)"
            if w != g:
                detail.append(f"  line {i + 1}: want {w!r}, got {g!r}")
                if len(detail) > 5:
                    detail.append("  ...")
                    break
        return Result(name, backend, False, "\n".join(detail), elapsed)

    return Result(name, backend, True, seconds=elapsed)


def main():
    parser = argparse.ArgumentParser(description="ppc regression tests")
    parser.add_argument("--ppc", default=os.path.join(ROOT, "build", "ppc"))
    parser.add_argument("--backend", action="append", default=None,
                        help="backend to test; repeatable (default: c)")
    parser.add_argument("--filter", default="", help="only run cases containing this")
    parser.add_argument("--opt", default=None, choices=["0", "1", "2"],
                        help="optimization level to compile at; optimization "
                             "must never change observable behaviour, so the "
                             "same expectations apply at every level")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    color = sys.stdout.isatty()
    backends = args.backend or ["c"]

    if not os.path.exists(args.ppc):
        print(f"cannot find the compiler at {args.ppc}; build it first")
        return 2

    sources = sorted(
        os.path.join(CASES, f) for f in os.listdir(CASES) if f.endswith(".pp"))
    if args.filter:
        sources = [s for s in sources if args.filter in os.path.basename(s)]
    if not sources:
        print("no test cases found")
        return 2

    results = []
    for backend in backends:
        for source in sources:
            try:
                result = run_case(args.ppc, source, backend, args.timeout, args.opt)
            except subprocess.TimeoutExpired:
                result = Result(os.path.basename(source)[:-3], backend, False,
                                f"timed out after {args.timeout}s")
            results.append(result)

            if result.skipped:
                continue
            if result.ok:
                mark = colorize("pass", GREEN, color)
                if args.verbose:
                    print(f"  {mark}  {result.name} [{backend}] "
                          f"{colorize(f'{result.seconds:.2f}s', DIM, color)}")
            else:
                print(f"  {colorize('FAIL', RED, color)}  {result.name} [{backend}]")
                for line in result.detail.splitlines():
                    print(f"        {line}")

    ran = [r for r in results if not r.skipped]
    failed = [r for r in ran if not r.ok]
    total_time = sum(r.seconds for r in ran)

    print()
    summary = f"{len(ran) - len(failed)}/{len(ran)} passed in {total_time:.1f}s"
    if failed:
        print(colorize(f"FAILED: {summary}", RED, color))
        print("  " + ", ".join(sorted({r.name for r in failed})))
        return 1
    print(colorize(f"ok: {summary}", GREEN, color))
    return 0


if __name__ == "__main__":
    sys.exit(main())
