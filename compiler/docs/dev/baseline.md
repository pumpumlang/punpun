# PPC baseline — measured before the hardening pass

Everything here was measured on the machine described below, with
`tests/benchmark.py --repeat 5`. Raw figures are in `baseline.json` and can be
reproduced with:

```sh
make benchmark
python3 tests/benchmark.py --compare docs/dev/baseline.json
```

Every number is a median of 5 runs after a discarded warm-up, with min and max
reported. A change under ~3% is inside run-to-run spread on this machine and is
not treated as a result.

## Environment

| | |
|---|---|
| Host | Linux x86-64, **1 core** |
| C++ compiler | GCC 13.3.0 |
| C compiler | GCC 13.3.0 |
| Clang | **not available** — the dual-compiler build could not be run |
| Binutils | GNU as/ld 2.42 |
| Python | 3.12.3 |

The single core matters: it means parallel-compilation work cannot be measured
honestly here, and wall-clock figures are effectively serial. Any future claim
about parallel speedup must be measured on a multi-core host.

## Build health

| Check | Result |
|---|---|
| Clean build, `-Wall -Wextra -Wpedantic` | **0 warnings** |
| Sanitizer build (ASan + UBSan) | builds and runs |
| Regression suite, 3 backends | 52/52 before fixes, **55/55** after |
| Regression suite under sanitizers | **1 failure found** — see below |

### Defect found by the sanitizer build

`AddressSanitizer: heap-buffer-overflow` in `Vm::invoke`, reading 8 bytes past
an enum block allocated by `pp_object_alloc`.

Root cause was not VM-specific. Match lowering extracted *every* pattern binding
before evaluating nested discriminant tests, so in

```punpun
match w {
    Wrap::Held(Option::Some(v)) => v,
    Wrap::Held(Option::None) => -1,
}
```

arm 1 projected `v` out of the payload before checking it was `Some`. Against a
`None` value that reads payload slot 0 of a block that only has a tag.

- Bytecode and native backends: out-of-bounds heap read.
- C backend: in-bounds but uninitialized read, because the union is sized to the
  largest variant. Wrong rather than crashing, which is why the optimized suite
  never caught it.

Fixed by replacing `HirArm`'s two parallel lists (bindings, test) with one
ordered `steps` sequence of tests and bindings, so a projection only ever runs
after the test that establishes its variant. Regression test:
`tests/cases/nested_patterns.pp`, which covers two- and three-level nesting plus
a guard.

### Defect found in the build graph

`make debug` emitted `overriding recipe for target 'ppc-debug'`. Two rules
existed for the same target — the parameterized `$(TARGET):` rule and an
explicit `ppc-debug:` rule — leaving which one ran up to make's resolution
order. The explicit rule also dropped sanitizer flags from the link line.
Fixed by deleting the duplicate and making `LDFLAGS` overridable.

## Compile time (median ms)

| Workload | check | build -O0 c | build -O2 c | build -O2 native | build bytecode |
|---|---|---|---|---|---|
| sieve | 1.8 | 34.8 | 35.6 | 21.0 | 1.9 |
| fib | 1.7 | 34.0 | 37.3 | 22.4 | 1.9 |
| match | 1.8 | 36.3 | 42.6 | 22.7 | 2.2 |
| generics | 1.8 | 36.6 | 37.5 | 22.4 | 1.9 |
| many_fns (400 functions) | 7.0 | 619.6 | 885.8 | **84.8** | 15.9 |

The C path's cost is dominated by the host compiler, not by PPC: `many_fns`
spends 885ms, of which PPC's own front end is 7ms. The native backend compiles
the same program in 84.8ms — **10x faster than the C path** — which is the whole
argument for its existence.

## Run time (median ms)

| Workload | c -O2 | c -O0 | native -O2 | bytecode |
|---|---|---|---|---|
| sieve | **7.5** | 11.8 | 9.4 | 49.1 |
| fib | **3.0** | 7.0 | 10.4 | 29.9 |
| match | **10.2** | 20.5 | 87.0 | 146.1 |
| generics | **2.2** | 3.2 | 39.9 | 73.8 |

This is the most important result in the baseline, and it is not flattering.

The native backend is competitive on `sieve` (1.25x slower than C) but falls off
badly elsewhere: **3.5x** slower on `fib`, **8.5x** on `match`, **18x** on
`generics`. The pattern is clear — workloads dominated by calls and aggregates
suffer, workloads dominated by array traffic do not.

Two causes, both known and both deliberate simplifications in the current
implementation:

1. **Every virtual value lives in a stack slot.** There is no register
   allocator, so each operation is load-operate-store. `sieve` hides this
   because its inner loop is already memory-bound; `fib` and `generics` do not.
2. **Aggregates are boxed on the heap** with a deep copy at every point value
   semantics require one. `match` and `generics` allocate constantly.

These are the targets for the register allocation and escape analysis work.
`generics` at 18x is the headline number to beat.

## Artifact size (bytes)

| Workload | c -O2 | native -O2 | bytecode |
|---|---|---|---|
| sieve | 38,496 | 38,448 | 3,382 |
| fib | 38,528 | 38,480 | 1,210 |
| match | 38,528 | 38,480 | 3,880 |
| generics | 38,496 | 38,600 | 2,425 |
| many_fns | 113,256 | **267,624** | 508,995 |

The ~38KB floor on the small programs is the statically linked runtime, not
generated code. `many_fns` is the informative row: native output is **2.4x
larger** than the C path's, which is the same stack-slot problem showing up as
code size. Bytecode is smallest for small programs and largest for `many_fns`
because its fixed-width instruction encoding does not compress.

## What this baseline commits us to

1. Native backend runtime is the biggest measurable weakness. Register
   allocation and escape analysis are justified by data, not intuition.
2. `check` is already fast (1.8ms typical, 7ms for 400 functions). New analysis
   must not erode this — it is the number a developer feels most.
3. The C backend's compile time is mostly the host compiler's. PPC-side
   optimizer work will barely move it; native-backend work will.
4. Any parallelism claim is unmeasurable on this host and must not be asserted.
