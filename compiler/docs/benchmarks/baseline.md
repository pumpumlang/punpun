# PPC performance baseline

Measured before the 1.3 hardening work began. Every later optimization is
judged against these numbers, and regressions are reported alongside wins.

## Method

- Harness: `tools/benchmark.py`, reproducible via
  `tools/benchmark.py --compare docs/benchmarks/baseline-full.json`.
- Every figure is the **median of 3 repetitions**, not a single sample.
- A warmup run precedes each measured set so the runtime-object cache is
  populated identically for every variant; otherwise whichever variant is
  measured first absorbs the cache-miss cost and looks artificially slow.
- Compile timings are **warm-cache**, because that is what a developer
  actually experiences on a rebuild.

## Host

- CPUs: 1 (single core — absolute numbers will differ on
  other machines; the ratios are the useful part)
- Compiler: GCC 13.3.0. Clang was not available, so the dual-toolchain build
  check in Phase 0 could not be performed and remains outstanding.
- PPC 1.0.1, language 1.0

## Compile time (ms, median)

| workload | check | c -O0 | c -O2 | native -O0 | native -O2 | bytecode -O2 |
|---|---|---|---|---|---|---|
| empty | 3 | 31 | 32 | 22 | 24 | 2 |
| fib_recursive | 3 | 39 | 39 | 25 | 25 | 2 |
| sieve | 3 | 36 | 36 | 22 | 22 | 2 |
| string_build | 3 | 34 | 37 | 22 | 23 | 2 |
| aggregates | 3 | 42 | 40 | 25 | 24 | 2 |
| matrix | 3 | 38 | 40 | 23 | 24 | 2 |
| enum_dispatch | 3 | 38 | 39 | 25 | 24 | 2 |
| generic_heavy | 2 | 38 | 39 | 23 | 25 | 2 |
| large_source | 8 | 534 | 668 | 74 | 82 | 10 |

Reading: the front end is 2-3 ms on ordinary programs and 8 ms on
`large_source` (400 functions), so front-end throughput is not currently a
bottleneck. Full compiles through the C backend are dominated by the host
compiler: 534-668 ms on `large_source` against 74-82 ms for the native
backend, which is the clearest argument for the native path existing.

## Generated program runtime (ms, median)

| workload | c -O0 | c -O2 | native -O0 | native -O2 | bytecode -O2 | native/C |
|---|---|---|---|---|---|---|
| empty * | 2 | 2 | 2 | 2 | 3 | **1.15x** |
| fib_recursive | 4 | 4 | 9 | 10 | 29 | **2.71x** |
| sieve | 11 | 6 | 10 | 10 | 60 | **1.50x** |
| string_build | 3 | 3 | 3 | 4 | 5 | **1.06x** |
| aggregates | 3 | 1 | 84 | 79 | 81 | **55.88x** |
| matrix | 4 | 4 | 4 | 4 | 14 | **0.94x** |
| enum_dispatch | 5 | 3 | 21 | 20 | 41 | **7.39x** |
| generic_heavy | 3 | 2 | 14 | 11 | 17 | **4.53x** |
| large_source * | 2 | 2 | 2 | 2 | 10 | **0.99x** |

`*` marks workloads too short for runtime to mean much; they mostly measure
process startup and should not be read as performance signal.

## What this says about priorities

The native backend's gap against C is **not** uniform, and the shape of it
contradicts the obvious assumption that register allocation is the main
problem:

| gap | workload | cause |
|---|---|---|
| 55.9x | aggregates | every struct is heap-boxed and deep-copied |
| 7.4x | enum_dispatch | enum values are boxed |
| 4.5x | generic_heavy | `Cell<T>` is boxed |
| 2.7x | fib_recursive | no register allocation; all values in stack slots |
| 1.5x | sieve | stack traffic in the loop body |
| 0.94x | matrix | already competitive — runtime-call bound, not codegen bound |

Aggregate boxing costs roughly an order of magnitude more than the missing
register allocator. Escape analysis is therefore the higher-value work, and
`matrix` shows that where allocation is absent the native backend is already
competitive with `gcc -O2`.

## Artifact size (bytes, -O2)

| workload | c | native | bytecode |
|---|---|---|---|
| empty | 38504 | 38448 | 306 |
| fib_recursive | 38536 | 38480 | 1214 |
| sieve | 38504 | 38448 | 3386 |
| string_build | 38504 | 38448 | 1774 |
| aggregates | 38504 | 42680 | 3952 |
| matrix | 38504 | 42544 | 5003 |
| enum_dispatch | 38544 | 42584 | 5386 |
| generic_heavy | 38504 | 38664 | 3922 |
| large_source | 88448 | 234856 | 436996 |

The native backend's `large_source` artifact (234 KB) is 2.7x the C
backend's (88 KB), because every value occupies a stack slot and each access
is a separate load/store. Register allocation should reduce both size and
time here.

## Build health at baseline

- Clean normal build: **0 warnings** (GCC 13.3, `-Wall -Wextra -Wpedantic`)
- Sanitizer build (`make debug`, ASan + UBSan): builds clean, no
  duplicate-target warning observed
- Regression suite: **55/55** across the C, native, and bytecode backends

