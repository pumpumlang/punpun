# Known limitations and deferred work

Written to be accurate rather than reassuring. Nothing here is claimed to work.

## Not implemented in this pass

### mem2reg — `promoted_locals` is a dead statistic

`PassStats::promoted_locals` exists and is summed, but no pass ever increments
it. `include/ppc/mir/mir.hpp` still says "mem2reg promotes the ones that never
escape", which is aspirational.

It was left rather than deleted because deleting it hides a real gap. It was not
implemented because doing it correctly requires an explicit `Phi` MIR operation,
and that touches the verifier, the dump format, all three backends, liveness,
and every existing optimizer pass. Introducing partial SSA to make a counter
move would be exactly the "incorrect SSA to increase a statistic" the brief
warns against.

The mitigating fact: the C backend hands locals to the host compiler as ordinary
C locals, so GCC's own SSA construction already performs this promotion on the
default path. The gap is real for the native and bytecode backends only.

### Register allocation

The native backend still places every value in a stack slot. The measured cost
is visible in `fib_recursive`, where native is 2.5x slower than C, and in
artifact size, where `large_source` is 2.7x the C backend's output.

The baseline showed this is **not** the largest native-backend problem —
aggregate boxing cost roughly an order of magnitude more, which is why escape
analysis was done first. Register allocation is the correct next piece of work.

### Interprocedural copy elimination

`analyze_parameters` is implemented and identifies read-only parameters, but no
backend consumes it yet. Wiring it in would let a caller pass a value struct
without the defensive deep copy when the callee provably only reads it. The
analysis is written and compiles; the call-site change is not done, so the
capability does **not** exist.

### Windows support

Both implementations now exist and the runtime routes through the abstraction.
The POSIX path is proven by the full suite. **The Windows path has never been
compiled**: no Windows toolchain and no way to install a cross-compiler were
available. Run `tools/check-windows.sh` before treating it as real. See
`docs/platform-support.md`.

### Recursive types

Rejected with E0901 rather than supported. The boxed backends could represent
them almost immediately; the C backend needs recursive payload fields emitted as
pointers. This blocks writing an AST in PunPun, and therefore self-hosting.

### Language service gaps

`definition` resolves only where the index records a target. Hover on a local's
*declaration* falls back to the enclosing function, because locals are not
indexed as declaration symbols; hover on a *use* works. References, rename, code
actions, signature help, and semantic highlighting are not implemented.

### Self-hosting

Not possible today. `docs/self-hosting.md` documents what was probed, the four
blockers, and an ordered plan. The largest blocker is the absence of any generic
collection: `nums` holds `int` and nothing else.

### Not attempted

Incremental compilation beyond the runtime-object cache; structured fix
suggestions in diagnostics; optimization remarks; the performance map; PGO;
native debug information.

## Measurement caveats

- All numbers come from a **single-core** container with a **18-22% noise
  floor** on runtime measurements of an unchanged binary. The 5% regression
  threshold is below measurable resolution here. Results should be reconfirmed
  on a quiet multi-core machine.
- **Clang was unavailable**, so the dual-toolchain build check was not
  performed. PPC has only been built with GCC 13.3.
- The escape analysis has not been measured on functions with thousands of
  allocations, where its fixpoint could become expensive. No compile-time budget
  is enforced on it yet.

## Correctness caveats

- Escape analysis assumes the consuming backend deep-copies value structs at
  every transfer. This holds for the native and bytecode backends and is
  documented in `include/ppc/mir/escape.hpp`. A future backend that shares value
  structs would silently invalidate the analysis. There is no assertion
  enforcing this precondition — that is a gap.
- Copy elision relies on a single-use count over registers. It has been tested
  against the differential suite and `tests/cases/escape_analysis.pp`, but has
  not been fuzzed.
