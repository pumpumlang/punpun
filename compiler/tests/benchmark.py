#!/usr/bin/env python3
"""ppc benchmark harness.

Measures compile time, runtime, and artifact size across backends and
optimization levels, and writes a JSON record so a later run can be diffed
against it.

Single measurements of a 3ms operation are noise. Every figure here is the
median of N repetitions after a discarded warm-up, and the spread is reported
alongside so a "improvement" smaller than the noise floor is visible as such.

Usage:
    tests/benchmark.py                       # run and print a table
    tests/benchmark.py --save baseline.json  # record for later comparison
    tests/benchmark.py --compare baseline.json
    tests/benchmark.py --repeat 9            # more samples, tighter medians
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

# Programs chosen to exercise different parts of the pipeline rather than to
# flatter any one of them: an allocation/array workload, a call-heavy
# recursive workload, a pattern-matching workload, and a generics workload.
WORKLOADS = [
    ("sieve", "bench_sieve.pp", """
launch {
    let limit = 200000;
    let flags = numbers();
    for i in 0..limit { push(flags, 0); }
    let mut count = 0;
    let mut n = 2;
    while n < limit {
        if flags[n] == 0 {
            count = count + 1;
            let mut m = n + n;
            while m < limit { put(flags, m, 1); m = m + n; }
        }
        n = n + 1;
    }
    say(count);
}
"""),
    ("fib", "bench_fib.pp", """
fn fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
launch { say(fib(27)); }
"""),
    ("match", "bench_match.pp", """
enum Shape { Circle(int), Rect(int, int), Empty }
fn area(s: Shape) -> int {
    return match s {
        Shape::Circle(r) => 3 * r * r,
        Shape::Rect(w, h) => w * h,
        Shape::Empty => 0,
    };
}
launch {
    let mut total = 0;
    for i in 0..300000 {
        total = total + area(Shape::Circle(i % 10));
        total = total + area(Shape::Rect(i % 7, 3));
        total = total + area(Shape::Empty);
    }
    say(total % 1000003);
}
"""),
    ("generics", "bench_generics.pp", """
fn identity<T: Copy>(value: T) -> T { return value; }
struct Box<T> { value: T, public fn get() -> T { return self.value; } }
fn total(rounds: int) -> int {
    let mut sum = 0;
    for i in 0..rounds {
        sum = sum + identity(i) + Box(i).get() + identity<int>(1);
    }
    return sum;
}
launch { say(total(200000) % 1000003); }
"""),
    # Compile-time only: many small functions, which stresses the front end and
    # the per-function cost of every pass rather than generated-code quality.
    ("many_fns", "bench_many_fns.pp", None),
]


def generate_many_functions(count=400):
    parts = []
    for i in range(count):
        parts.append(
            f"fn helper{i}(a: int, b: int) -> int {{\n"
            f"    let x = a * {i + 1} + b;\n"
            f"    if x % 2 == 0 {{ return x / 2; }}\n"
            f"    return x * 3 + 1;\n"
            f"}}\n")
    calls = " + ".join(f"helper{i}({i}, {i + 1})" for i in range(count))
    parts.append(f"launch {{ say(({calls}) % 1000003); }}\n")
    return "".join(parts)


def timed(argv, repeat, warmup=1):
    """Median wall time in ms, plus min and max, over `repeat` runs."""
    for _ in range(warmup):
        subprocess.run(argv, capture_output=True)
    samples = []
    for _ in range(repeat):
        start = time.perf_counter()
        proc = subprocess.run(argv, capture_output=True)
        samples.append((time.perf_counter() - start) * 1000.0)
        if proc.returncode != 0:
            return None, None, None, proc.stderr.decode()[:200]
    return statistics.median(samples), min(samples), max(samples), None


def main():
    parser = argparse.ArgumentParser(description="ppc benchmarks")
    parser.add_argument("--ppc", default=PPC)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--save")
    parser.add_argument("--compare")
    parser.add_argument("--filter", default="")
    args = parser.parse_args()

    if not os.path.exists(args.ppc):
        print(f"cannot find the compiler at {args.ppc}; run make first")
        return 2

    work = tempfile.mkdtemp(prefix="ppc-bench-")
    sources = {}
    for name, filename, body in WORKLOADS:
        path = os.path.join(work, filename)
        with open(path, "w") as handle:
            handle.write(body if body is not None else generate_many_functions())
        sources[name] = path

    results = {}
    print(f"ppc benchmarks — median of {args.repeat} runs, 1 warm-up discarded\n")

    # -- compile time -----------------------------------------------------
    print(f"{'workload':<12} {'mode':<22} {'median':>9} {'min':>8} {'max':>8}")
    print("-" * 63)
    for name, _, _ in WORKLOADS:
        if args.filter and args.filter not in name:
            continue
        source = sources[name]
        for label, argv in [
            ("check", [args.ppc, "check", source]),
            ("build -O0 c", [args.ppc, "build", "-O0", "--backend=c", "-o",
                             os.path.join(work, name + "_o0"), source]),
            ("build -O2 c", [args.ppc, "build", "-O2", "--backend=c", "-o",
                             os.path.join(work, name + "_o2"), source]),
            ("build -O2 native", [args.ppc, "build", "-O2", "--backend=native", "-o",
                                  os.path.join(work, name + "_nat"), source]),
            ("build bytecode", [args.ppc, "build", "-O2", "--backend=bytecode", "-o",
                                os.path.join(work, name + "_bc"), source]),
        ]:
            median, low, high, error = timed(argv, args.repeat)
            key = f"compile/{name}/{label}"
            if error:
                print(f"{name:<12} {label:<22} {'FAILED':>9}  {error.splitlines()[0][:30]}")
                results[key] = None
                continue
            results[key] = median
            print(f"{name:<12} {label:<22} {median:8.1f}ms {low:7.1f} {high:7.1f}")

    # -- run time ---------------------------------------------------------
    print(f"\n{'workload':<12} {'backend':<22} {'median':>9} {'min':>8} {'max':>8}")
    print("-" * 63)
    for name, _, _ in WORKLOADS:
        if args.filter and args.filter not in name:
            continue
        if name == "many_fns":
            continue  # compile-time workload; its runtime is meaningless
        for label, argv in [
            ("c -O2", [os.path.join(work, name + "_o2")]),
            ("c -O0", [os.path.join(work, name + "_o0")]),
            ("native -O2", [os.path.join(work, name + "_nat")]),
            ("bytecode", [args.ppc, "run", "--backend=bytecode", sources[name]]),
        ]:
            if not label.startswith("bytecode") and not os.path.exists(argv[0]):
                continue
            median, low, high, error = timed(argv, args.repeat)
            key = f"run/{name}/{label}"
            if error:
                print(f"{name:<12} {label:<22} {'FAILED':>9}")
                results[key] = None
                continue
            results[key] = median
            print(f"{name:<12} {label:<22} {median:8.1f}ms {low:7.1f} {high:7.1f}")

    # -- artifact size ----------------------------------------------------
    print(f"\n{'workload':<12} {'artifact':<22} {'bytes':>10}")
    print("-" * 46)
    for name, _, _ in WORKLOADS:
        if args.filter and args.filter not in name:
            continue
        for label, suffix in [("c -O2", "_o2"), ("native -O2", "_nat"),
                              ("bytecode", "_bc.ppb")]:
            path = os.path.join(work, name + suffix)
            if not os.path.exists(path):
                continue
            size = os.path.getsize(path)
            results[f"size/{name}/{label}"] = size
            print(f"{name:<12} {label:<22} {size:10d}")

    if args.save:
        with open(args.save, "w") as handle:
            json.dump({"results": results, "repeat": args.repeat}, handle, indent=2)
        print(f"\nsaved to {args.save}")

    if args.compare:
        with open(args.compare) as handle:
            before = json.load(handle)["results"]
        print(f"\n{'metric':<44} {'before':>9} {'after':>9} {'change':>9}")
        print("-" * 75)
        regressions = []
        for key in sorted(set(before) | set(results)):
            old = before.get(key)
            new = results.get(key)
            if old is None or new is None or old == 0:
                continue
            delta = (new - old) / old * 100.0
            # A change smaller than typical run-to-run spread is not a result.
            marker = "" if abs(delta) < 3.0 else ("  WORSE" if delta > 0 else "  better")
            print(f"{key:<44} {old:9.1f} {new:9.1f} {delta:+8.1f}%{marker}")
            if delta > 5.0:
                regressions.append((key, delta))
        if regressions:
            print(f"\n{len(regressions)} metric(s) regressed by more than 5%:")
            for key, delta in regressions:
                print(f"  {key}  {delta:+.1f}%")

    return 0


if __name__ == "__main__":
    sys.exit(main())
