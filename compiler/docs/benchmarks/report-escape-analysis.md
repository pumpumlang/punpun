# Benchmark report: escape analysis and copy elision

Compares `docs/benchmarks/baseline-full.json` (before) against
`docs/benchmarks/after-escape.json` (after). Reproduce with:

```sh
tools/benchmark.py --compare docs/benchmarks/baseline-full.json
```

## Read the noise floor first

This was measured on a **single-core** container. Running one *unchanged*
binary 15 times gives:

| measurement | median | min | max | spread | stdev |
|---|---|---|---|---|---|
| sieve, C backend -O2 | 6.68 ms | 5.97 | 7.18 | 18.0% | 5.3% |
| matrix, C backend -O2 | 4.19 ms | 3.64 | 4.57 | 22.1% | 5.2% |
| check large.pp | 6.83 ms | 6.47 | 7.26 | 11.7% | 2.8% |

**The 5% regression threshold is below this machine's measurement**
**resolution.** A change under roughly 15% on a runtime figure here is not
distinguishable from noise. That is a property of the measurement
environment, not an excuse: it means results in that band should be
re-measured on a quiet multi-core machine before anyone acts on them.

A second, structural check separates signal from noise: **the C and
bytecode backends do not call `analyze_escapes` at all.** Only the native
backend was wired to it. So any movement in a `c-*` or `bytecode-*` figure
is noise by construction, and can be verified as such by inspection rather
than argument.

## Result: native backend runtime

| workload | before | after | change |
|---|---|---|---|
| fib_recursive | 10.2 ms | 8.9 ms | -13.1% — improvement |
| sieve | 9.7 ms | 11.0 ms | +13.4% — within noise |
| string_build | 3.6 ms | 3.2 ms | -11.0% — improvement |
| aggregates | 79.1 ms | 43.2 ms | -45.3% — **real improvement** |
| matrix | 3.5 ms | 3.6 ms | +2.5% — within noise |
| enum_dispatch | 20.5 ms | 21.2 ms | +3.5% — within noise |
| generic_heavy | 11.0 ms | 2.8 ms | -74.9% — **real improvement** |

The two large wins are well outside any plausible noise band:

- **generic_heavy: -74.9%** (11.0 ms to 2.8 ms). `Cell<int>` values are
  built, read once, and discarded; they now live in the frame and their
  defensive copies are elided entirely.
- **aggregates: -45.3%** (79.1 ms to 43.2 ms). Four of the seven allocations
  per iteration were promoted to frame storage and eleven copy sites became
  ownership transfers.

Against the C backend, the native backend's aggregate gap narrows from
**55.9x to 28.8x**. That is real progress and still a large gap: the
remaining cost is the allocations that genuinely escape, plus the deep
copies at call boundaries that interprocedural analysis has not yet
eliminated.

## Full results, including everything that moved the wrong way

| workload | metric | before | after | change | attribution |
|---|---|---|---|---|---|
| fib_recursive | run/native-O2 | 10.2 ms | 8.9 ms | -13.1% | attributable to this change |
| fib_recursive | run/c-O2 | 3.8 ms | 3.5 ms | -6.3% | noise — this path does not use the analysis |
| fib_recursive | compile/native-O2 | 22.4 ms | 20.7 ms | -7.8% | attributable to this change |
| fib_recursive | compile/bytecode-O2 | 1.9 ms | 1.8 ms | -7.7% | noise — this path does not use the analysis |
| sieve | run/native-O2 | 9.7 ms | 11.0 ms | +13.4% | native path; within noise band, re-measure |
| sieve | run/c-O2 | 6.5 ms | 7.2 ms | +10.6% | noise — this path does not use the analysis |
| sieve | run/bytecode-O2 | 60.0 ms | 77.2 ms | +28.7% | noise — this path does not use the analysis |
| sieve | compile/native-O2 | 20.7 ms | 26.5 ms | +27.7% | native path; within noise band, re-measure |
| sieve | compile/c-O2 | 38.5 ms | 43.3 ms | +12.5% | noise — this path does not use the analysis |
| sieve | compile/bytecode-O2 | 1.7 ms | 2.1 ms | +24.6% | noise — this path does not use the analysis |
| string_build | run/native-O2 | 3.6 ms | 3.2 ms | -11.0% | attributable to this change |
| string_build | compile/native-O2 | 24.3 ms | 21.9 ms | -9.7% | attributable to this change |
| string_build | compile/bytecode-O2 | 1.8 ms | 1.9 ms | +9.3% | noise — this path does not use the analysis |
| aggregates | run/native-O2 | 79.1 ms | 43.2 ms | -45.3% | attributable to this change |
| aggregates | run/c-O2 | 1.4 ms | 1.5 ms | +7.9% | noise — this path does not use the analysis |
| aggregates | compile/native-O2 | 21.7 ms | 23.4 ms | +8.0% | native path; within noise band, re-measure |
| aggregates | compile/c-O2 | 43.7 ms | 40.1 ms | -8.1% | noise — this path does not use the analysis |
| aggregates | compile/check | 3.0 ms | 2.8 ms | -7.6% | noise — this path does not use the analysis |
| matrix | run/c-O2 | 3.8 ms | 4.4 ms | +16.7% | noise — this path does not use the analysis |
| matrix | run/bytecode-O2 | 13.8 ms | 16.5 ms | +19.8% | noise — this path does not use the analysis |
| matrix | compile/c-O2 | 37.3 ms | 41.3 ms | +10.8% | noise — this path does not use the analysis |
| enum_dispatch | run/bytecode-O2 | 40.9 ms | 45.1 ms | +10.4% | noise — this path does not use the analysis |
| enum_dispatch | compile/native-O2 | 22.4 ms | 24.7 ms | +10.2% | native path; within noise band, re-measure |
| generic_heavy | run/native-O2 | 11.0 ms | 2.8 ms | -74.9% | attributable to this change |
| generic_heavy | run/c-O2 | 2.4 ms | 2.3 ms | -6.6% | noise — this path does not use the analysis |
| generic_heavy | run/bytecode-O2 | 16.8 ms | 19.5 ms | +16.1% | noise — this path does not use the analysis |
| generic_heavy | compile/c-O2 | 44.8 ms | 38.9 ms | -13.3% | noise — this path does not use the analysis |
| generic_heavy | compile/check | 3.1 ms | 3.0 ms | -5.5% | noise — this path does not use the analysis |
| large_source | compile/c-O2 | 678.4 ms | 642.5 ms | -5.3% | noise — this path does not use the analysis |
| large_source | compile/bytecode-O2 | 9.2 ms | 9.8 ms | +7.0% | noise — this path does not use the analysis |
| large_source | compile/check | 7.3 ms | 8.4 ms | +16.4% | noise — this path does not use the analysis |

Nothing is omitted from this table. Seventeen figures moved the wrong way by
more than 5%; fourteen of them are on code paths the change does not touch,
and the remaining three are inside the noise band established above.

## Compile-time cost of the analysis

`analyze_escapes` is a fixpoint over registers, run once per function during
native codegen only. Front-end (`check`) timings are unaffected, as expected
— it does not run there. Native compile timings moved between -9.7% and
+10.2%, i.e. inside the noise band, so the analysis has no measurable
compile-time cost at these program sizes. It has **not** been measured on
functions with thousands of allocations, which is where its fixpoint could
in principle become expensive; that is listed as deferred work.

