# PunPun Step 7 completion report

Version: `0.7.0-dev.5`
Milestone: Step 7 / PunPun 0.7 compiler scalability and backend maturity
Status: implementation complete on the Linux x86-64 development host; release promotion remains separately platform-gated

## 7.1 — Machine IR and ABI

PunPun now has a verified target-aware Machine IR below MIR. It defines the internal PunPun argument-block ABI, the modeled SysV AMD64 native ABI, hidden record result pointers, call barriers, physical locations, spills, callee-save requirements and frame constraints. `emit-machine-ir` and `emit-abi` expose these decisions for testing/debugging.

## 7.2 — backend decoupling

Canonical binding IDs and explicit HIR/MIR operations carry lexical identity, move/drop behavior and all currently supported aggregate/address/index/list/await semantics into Machine IR. Direct x86-64 instruction selection no longer walks typed source statements/expressions. The old source-body emitter was removed from `backend_x86_64.hpp`.

Regression coverage runs structs, enum payload/match, list mutation, short-circuit control flow, calls and scalar arithmetic through the Machine-IR body emitter.

## 7.3 — granular incremental compilation

The driver emits independently assemblable function units and caches one native object per function. Cache identity includes:

- target/toolchain/optimization configuration;
- the function's own interface hash;
- direct dependency function-interface and reachable shape-layout hashes;
- Machine-IR body/debug/allocation hash.

An unrelated function interface/body edit can therefore preserve unaffected objects. `--cache-info` explains misses, `--stats` exposes real reused/rebuilt counts, and `scripts/benchmark_projects.py --gate` asserts exact one-function rebuild behavior on a deterministic generated project.

## 7.4 — optimizer and allocator

Machine IR optimization includes local propagation/cleanup, constant branch simplification and unreachable block pruning. Structural verification runs around optimization and final verification checks allocation/frame/call-clobber constraints.

The direct emitter now reads physical Machine-IR register homes and spill slots. Call-live values use preserved GPRs or spills; callee-saved registers are pushed/popped explicitly. Straight-line spill lifetimes can reuse ranges. CFG-crossing values are conservatively reserved in dedicated ranges to avoid unsafe coalescing caused by non-dominance block order.

## 7.5 — ergonomics/quality gates

- stable source diagnostics and machine-applicable replacements where available;
- top-level private-function module boundary enforcement;
- inspectable/documented FFI/ABI contract;
- lint warnings for formatting drift and duplicate imports;
- byte-reproducible PPX publish archives;
- CI incremental/scalability gate and compatibility regression coverage.

## Validation

The final validation results for this handoff are recorded after the full clean test/self-host/docs/privacy/benchmark pass. Platform qualification is deliberately not inferred from local Linux validation.
