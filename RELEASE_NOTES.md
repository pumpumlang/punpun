# PunPun 0.7.0-dev.5 — Step 7 complete

`0.7.0-dev.5` completes the planned Step 7 / PunPun 0.7 compiler-scalability and backend-maturity milestone on the Linux x86-64 development host. It is intentionally still a development snapshot: platform promotion remains gated separately.

## Backend architecture

The direct x86-64 backend is now driven exclusively by verified Machine IR for PunPun function bodies. Machine IR explicitly carries the operations and storage identities required for aggregates, enums/match, moves/drops, addresses, members, indexed mutation, lists, short-circuit CFG and async/await lowering. The prior typed-source body emitter has been removed.

Machine IR also owns target ABI facts, call barriers, physical register/stack locations, spill ranges and callee-save requirements. The native emitter consumes those allocations directly. Values that cross helper calls use call-safe locations; CFG-crossing values currently use conservative dedicated spill ranges rather than unsafe lifetime coalescing.

## Incremental compilation

Direct-native builds cache independently assemblable function objects. A function object key contains target/toolchain/optimization configuration plus the function's own interface, body/debug mapping and direct dependency ABI/layout hash. Editing an unrelated function no longer invalidates the entire native object set.

`--cache-info` reports function-level hits/misses with reasons, `--stats` reports reused/rebuilt function/module counts, and the generated large-project benchmark has an exact incremental gate: a one-function edit must rebuild exactly one function.

## Optimizer/allocation

Machine IR now performs verified local copy/load propagation, redundant store/dead-move cleanup, constant branch simplification and unreachable block pruning. Allocation includes call-aware register constraints, explicit callee-save emission, allocator-owned register homes, spill ranges and safe stack-slot reuse for non-overlapping straight-line lifetimes.

## Developer experience and ecosystem

- Unknown-name suggestions can expose machine-applicable fix-it replacements.
- Cross-module calls to `private fn` are rejected with a stable diagnostic.
- `pp lint` detects formatting drift and duplicate imports.
- `ppc emit-abi` exposes the implemented internal/native ABI contract for inspection.
- PPX publish ZIPs use deterministic path ordering, timestamps and metadata, making the same package bytes reproducible across mtime-only changes.
- CI includes the incremental compilation gate in addition to the normal compiler, docs and hygiene tests.

## Release truthfulness

The earlier `0.6.0-beta.1` Arch qualification failure remains unresolved unless a later real Arch workflow proves otherwise. Finishing Step 7 does not waive platform gates. `0.7.0-dev.5` therefore records compiler milestone completion without pretending to be a fully qualified 0.7 beta.
