# PunPun 0.7 incremental compilation contract

## Goal

A direct-native edit should rebuild only native function objects whose own code or required ABI/layout dependencies changed. Whole-program link output may still be replaced atomically after object reuse.

## Function identity

Each non-extern function has three independent compiler fingerprints:

- `interface_hash`: the function's callable interface;
- `body_hash`: verified Machine-IR body, source/debug mapping and allocation identity;
- `dependency_hash`: interfaces of directly called known functions plus reachable struct/object/enum layouts used by the function/call ABI.

The function-object key also includes PunPun/compiler version, target, optimization mode and selected native toolchain identity.

## Required invalidation

A function object must rebuild when any of these change:

- target/toolchain/optimization configuration;
- its own interface;
- its direct dependency ABI/layout fingerprint;
- its body/debug/allocation fingerprint;
- the cached object is absent/corrupt.

An unrelated function body/interface change must not invalidate a function that neither calls it nor depends on its ABI/layout.

## Observability

`--cache-info` reports `FUNCTION HIT` / `FUNCTION MISS` and the miss reason. `--stats` reports reused/rebuilt function and module counts.

`scripts/benchmark_projects.py --gate` is the 0.7 regression gate for the deterministic generated workload. After warming the cache, editing exactly one generated function body must report exactly one rebuilt function and all other generated functions/main reused.

## Safety boundary

Cache reuse is never allowed to weaken semantic, ownership, IR or ABI validation. A failed rebuild must not execute stale output, and target/toolchain/ABI changes must invalidate affected objects.
