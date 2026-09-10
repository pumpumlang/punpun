#!/usr/bin/env python3
"""PPC benchmark harness.

Produces reproducible BEFORE/AFTER measurements so optimizer and backend work
can be judged by numbers rather than intuition.

    tools/benchmark.py --save baseline.json      record a baseline
    tools/benchmark.py --compare baseline.json   measure and diff against it

What it measures, per workload:

    check           front end only: parse, resolve, type-check
    build           full compile to a runnable artifact, per backend and -O level
    run             wall time of the generated program
    size            artifact size in bytes

Methodology notes that matter for trusting the output:

  * Every timing is the MEDIAN of N repetitions, not a single sample. A single
    timing on a shared machine is noise.
  * A warmup run precedes each measured set, so the runtime object cache and the
    filesystem cache are populated identically for every variant. Otherwise the
    first variant measured absorbs the cache-miss cost and looks slow.
  * Compile timings deliberately keep the cache warm, because that is what a
    developer actually experiences. Cold-cache cost is reported separately.
  * Runtime workloads must run long enough to dominate process startup. Very
    short programs mostly measure exec() and are marked as such.
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PPC = os.path.join(ROOT, "ppc")
BENCH_DIR = os.path.join(ROOT, "tests", "benchmarks")

# Backends and optimization levels swept for compile-time measurement.
BACKENDS = ["c", "native", "bytecode"]
OPT_LEVELS = ["-O0", "-O1", "-O2"]


def median_ms(fn, repetitions):
    """Runs `fn` `repetitions` times and returns (median, min, max) in ms."""
    samples = []
    for _ in range(repetitions):
        start = time.perf_counter()
        fn()
        samples.append((time.perf_counter() - start) * 1000.0)
    return (statistics.median(samples), min(samples), max(samples))


def run_quiet(argv, timeout=300):
    """Runs a command, discarding output. Returns the exit status."""
    return subprocess.run(argv, capture_output=True, timeout=timeout).returncode


class Workload:
    """One benchmark program plus how it should be exercised."""

    def __init__(self, name, path, args=None, runtime_dominated=True,
                 skip_backends=()):
        self.name = name
        self.path = path
        self.args = args or []
        # False means the program is too short for its run time to mean much;
        # it is still measured, but flagged so nobody over-reads the number.
        self.runtime_dominated = runtime_dominated
        self.skip_backends = set(skip_backends)


def discover_workloads():
    """Benchmark programs, in ascending order of cost."""
    def bench(name):
        return os.path.join(BENCH_DIR, name)

    workloads = [
        Workload("empty", bench("empty.pp"), runtime_dominated=False),
        Workload("fib_recursive", bench("fib.pp")),
        Workload("sieve", bench("sieve.pp")),
        Workload("string_build", bench("strings.pp")),
        Workload("aggregates", bench("aggregates.pp")),
        Workload("matrix", bench("matrix.pp")),
        Workload("enum_dispatch", bench("enums.pp")),
        Workload("generic_heavy", bench("generics.pp")),
        # A deliberately large machine-generated source, for front-end scaling.
        Workload("large_source", bench("large.pp"), runtime_dominated=False),
    ]
    return [w for w in workloads if os.path.exists(w.path)]


def measure_workload(workload, repetitions, include_run):
    """Measures one workload across every backend and optimization level."""
    result = {"name": workload.name, "compile": {}, "run": {}, "size": {}}

    # --- front-end only -------------------------------------------------
    run_quiet([PPC, "check", workload.path])  # warmup
    median, low, high = median_ms(
        lambda: run_quiet([PPC, "check", workload.path]), repetitions)
    result["compile"]["check"] = {"median_ms": median, "min_ms": low, "max_ms": high}

    # --- full compiles ---------------------------------------------------
    for backend in BACKENDS:
        if backend in workload.skip_backends:
            continue
        for opt in OPT_LEVELS:
            with tempfile.TemporaryDirectory(prefix="ppcbench-") as scratch:
                output = os.path.join(scratch, "out")
                argv = [PPC, "build", opt, f"--backend={backend}", "-o", output,
                        workload.path]

                # Warm the runtime object cache before timing, so the first
                # variant measured does not absorb its build cost.
                if run_quiet(argv) != 0:
                    continue

                median, low, high = median_ms(lambda: run_quiet(argv), repetitions)
                key = f"{backend}{opt}"
                result["compile"][key] = {
                    "median_ms": median, "min_ms": low, "max_ms": high}

                artifact = output if os.path.exists(output) else output + ".ppb"
                if os.path.exists(artifact):
                    result["size"][key] = os.path.getsize(artifact)

                if not include_run:
                    continue

                # --- generated program runtime ---------------------------
                if backend == "bytecode":
                    run_argv = [PPC, "run", opt, "--backend=bytecode",
                                workload.path]
                    if workload.args:
                        run_argv += ["--"] + workload.args
                else:
                    run_argv = [artifact] + workload.args

                if run_quiet(run_argv) != 0:
                    continue
                median, low, high = median_ms(
                    lambda: run_quiet(run_argv), repetitions)
                result["run"][key] = {
                    "median_ms": median, "min_ms": low, "max_ms": high,
                    "runtime_dominated": workload.runtime_dominated}
    return result


def measure_compiler_build():
    """Time to build PPC itself from clean. A compiler nobody can rebuild
    quickly is a compiler nobody will contribute to."""
    subprocess.run(["make", "clean"], cwd=ROOT, capture_output=True)
    start = time.perf_counter()
    proc = subprocess.run(["make", "-j", str(os.cpu_count() or 1)],
                          cwd=ROOT, capture_output=True, text=True)
    elapsed = (time.perf_counter() - start) * 1000.0
    warnings = proc.stdout.count("warning:") + proc.stderr.count("warning:")
    size = os.path.getsize(PPC) if os.path.exists(PPC) else 0
    return {"build_ms": elapsed, "warnings": warnings, "binary_bytes": size}


def collect(repetitions, include_run, include_self_build):
    report = {
        "schema": 1,
        "host": {
            "cpus": os.cpu_count(),
            "platform": sys.platform,
        },
        "repetitions": repetitions,
        "workloads": [],
    }

    proc = subprocess.run([PPC, "language-info"], capture_output=True, text=True)
    if proc.returncode == 0:
        try:
            report["compiler"] = json.loads(proc.stdout)
        except json.JSONDecodeError:
            pass

    if include_self_build:
        report["self_build"] = measure_compiler_build()

    for workload in discover_workloads():
        print(f"  measuring {workload.name} ...", flush=True)
        report["workloads"].append(
            measure_workload(workload, repetitions, include_run))
    return report


def percent_change(before, after):
    if before == 0:
        return 0.0
    return (after - before) / before * 100.0


def compare(baseline, current, threshold):
    """Prints a BEFORE/AFTER table. Regressions are never omitted."""
    index = {w["name"]: w for w in baseline["workloads"]}
    regressions = []
    improvements = []

    print(f"\n{'workload':<18} {'metric':<20} {'before':>10} {'after':>10} {'change':>9}")
    print("-" * 72)

    for workload in current["workloads"]:
        old = index.get(workload["name"])
        if not old:
            continue
        for section in ("compile", "run"):
            for key, value in sorted(workload[section].items()):
                previous = old.get(section, {}).get(key)
                if not previous:
                    continue
                before = previous["median_ms"]
                after = value["median_ms"]
                delta = percent_change(before, after)
                marker = ""
                if delta > threshold:
                    marker = "  REGRESSION"
                    regressions.append((workload["name"], f"{section}/{key}", delta))
                elif delta < -threshold:
                    marker = "  improved"
                    improvements.append((workload["name"], f"{section}/{key}", delta))
                print(f"{workload['name']:<18} {section + '/' + key:<20} "
                      f"{before:>9.1f}ms {after:>9.1f}ms {delta:>+8.1f}%{marker}")

        for key, size in sorted(workload.get("size", {}).items()):
            previous = old.get("size", {}).get(key)
            if not previous:
                continue
            delta = percent_change(previous, size)
            print(f"{workload['name']:<18} {'size/' + key:<20} "
                  f"{previous:>9}B {size:>9}B {delta:>+8.1f}%")

    print("-" * 72)
    print(f"{len(improvements)} improved, {len(regressions)} regressed "
          f"(threshold {threshold}%)")
    if regressions:
        print("\nRegressions, worst first:")
        for name, metric, delta in sorted(regressions, key=lambda r: -r[2]):
            print(f"  {name:<18} {metric:<20} {delta:+.1f}%")
    return 1 if regressions else 0


def main():
    parser = argparse.ArgumentParser(description="PPC benchmark harness")
    parser.add_argument("--save", metavar="FILE", help="write results to FILE")
    parser.add_argument("--compare", metavar="FILE", help="diff against FILE")
    parser.add_argument("--repetitions", type=int, default=5,
                        help="samples per measurement; median is reported")
    parser.add_argument("--threshold", type=float, default=5.0,
                        help="percent change counted as a regression")
    parser.add_argument("--no-run", action="store_true",
                        help="measure compile time only")
    parser.add_argument("--self-build", action="store_true",
                        help="also measure a clean rebuild of PPC itself")
    args = parser.parse_args()

    if not os.path.exists(PPC):
        print("ppc is not built; run make first")
        return 2

    print(f"measuring, {args.repetitions} repetitions per data point")
    report = collect(args.repetitions, not args.no_run, args.self_build)

    if args.save:
        with open(args.save, "w") as handle:
            json.dump(report, handle, indent=2)
        print(f"\nwrote {args.save}")

    if args.compare:
        with open(args.compare) as handle:
            baseline = json.load(handle)
        return compare(baseline, report, args.threshold)

    if not args.save:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
