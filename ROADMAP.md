# PunPun roadmap

PunPun is being developed toward Java-level reliability and C++-class native performance without hiding platform limitations or unfinished compiler work.

## 0.6 — language foundations

| Step | Scope | State |
| --- | --- | --- |
| 1 | Canonical versioning, green baseline, frozen generic/enum/nullability/ownership rules, parser scaffolding and compatibility gates | Complete in `0.6.0-dev.1` |
| 2 | Generic declarations/calls, constraints, monomorphization and deterministic specialization caching | Complete in `0.6.0-dev.3` |
| 3 | Algebraic enums, `Option<T>`, `Result<T,E>`, nested destructuring, exhaustive matching and `?` | Complete in `0.6.0-dev.3` |
| 4 | Move-state dataflow, lexical destruction, stronger borrows and checked `Slice<int>` views | Complete in `0.6.0-beta` |
| 5 | Mandatory verified HIR → MIR pipeline, MIR-authoritative function scheduling, function/interface/body fingerprints | Complete in `0.6.0-beta` |
| 5.5 | Optional Clang/LLVM compatibility backend and LLVM IR emission without replacing the native PunPun frontend | Complete in `0.6.0-beta` |
| 6 | Standard-library/runtime integration, compatibility regression gate, self-host verification, release qualification and packaging | Complete on the Linux x86-64 beta release host |

The normative 0.6 decisions live in [`spec/0.6/`](spec/0.6/). Platform-specific qualification remains explicit: the Linux host cannot honestly certify Windows installer execution or `pacman` install/upgrade/remove behavior.

### 0.6 backend choices

- **Direct PunPun x86-64:** default Linux x86-64 path and the project-owned native backend.
- **Portable C:** `--cc-backend`, useful for portability/toolchain integration.
- **Optional LLVM:** `--llvm-backend` uses Clang/LLVM after the same PunPun parser, semantic, ownership, HIR and MIR pipeline. In 0.6 this path is native-host only.

LLVM is an alternative code-generation path, not a replacement parser/type checker and not the ordinary PunPun compilation pipeline.

## 0.6.0-beta.1 release-hardening status

`0.6.0-beta.1` repaired the PP brand pipeline, source/release hygiene, package metadata, continuous CI, governance files and build → qualify → promote publishing gate. Its source is the clean baseline for 0.7 development.

The beta.1 promotion gate is intentionally separate from compiler development. The first Arch qualification attempt failed before package qualification completed, so beta.1 must not be described as fully platform-qualified. The release publisher correctly blocks promotion when any required platform job fails. Development may continue on `main` while that platform-specific release issue is repaired; release claims remain conservative.

## Step 7 / PunPun 0.7 — compiler scalability and backend maturity

Step 7 is **implementation-complete in `0.7.0-dev.5` on the Linux x86-64 development host**. This is a development milestone, not a claim that the 0.7 release has passed Arch/Windows promotion gates.

### Phase 7.1 — Machine IR and explicit ABI — complete

- Added verified target-aware Machine IR below MIR.
- Made PunPun argument-block and SysV AMD64 ABI locations explicit, including hidden result pointers for by-value records.
- Added call barriers, liveness, value classes, physical locations, callee-save requirements, spills and frame verification.
- Added `ppc emit-machine-ir` and `ppc emit-abi` for inspectable compiler/ABI state.

### Phase 7.2 — complete backend decoupling — complete

- Canonical lexical binding IDs now prevent shadowed source names from aliasing backend storage.
- HIR/MIR/Machine IR explicitly represent move-loads, drops, aggregate/enum construction, member/address/deref/index stores, lists, async/await and CFG-based short-circuit/match behavior.
- The direct x86-64 backend emits **every non-extern PunPun function body from verified Machine IR**. The old typed-source body emitter has been removed.
- Machine IR call metadata is the authority for internal argument blocks and native ABI placement.

### Phase 7.3 — granular incremental compilation — complete

- Direct-native builds assemble/cache one object per PunPun function plus independent process-entry glue.
- Function cache identity is split into interface, body/debug mapping and direct dependency ABI/layout hashes.
- Unrelated function/interface changes no longer invalidate every native function object.
- `--cache-info` reports function-level hits/misses and precise invalidation reasons; `--stats` reports function/module reused/rebuilt counts.
- `scripts/benchmark_projects.py --gate` verifies a one-function edit rebuilds exactly that function in the generated large-project workload and is enforced in CI.

### Phase 7.4 — optimizer and allocation maturity — complete

- Machine IR performs local load/copy propagation, redundant store/dead move cleanup, constant-branch simplification and unreachable-block pruning, with structural verification before and after optimization.
- The direct backend consumes allocator-owned physical register homes and spill ranges rather than assigning every virtual value a redundant frame home.
- Call-live values avoid volatile registers; required callee-saved registers are emitted/restored explicitly.
- Straight-line spill ranges are reused when lifetimes do not overlap. CFG-crossing values are conservatively assigned dedicated stack ranges until a future interference-graph allocator can coalesce them safely.
- Verification rejects overlapping register/stack allocations, invalid CFG targets, undefined values, call-clobber violations and out-of-frame spills.

### Phase 7.5 — everyday compiler ergonomics — complete

- Diagnostics expose source spans, stable error codes, help text and machine-applicable fix-its where a concrete replacement is known.
- Top-level `private fn` visibility is enforced across modules; object/field/method visibility continues to be checked by semantic analysis.
- `ppc emit-abi` exposes the implemented PunPun internal ABI and supported SysV native ABI contract; `spec/0.7/ffi-abi.md` documents the stability boundary.
- `pp lint` now gates formatting drift (`W2001`) and duplicate imports (`W2002`).
- PPX publish archives are byte-reproducible across source mtime changes through canonical ordering, metadata and ZIP timestamps.
- Incremental scalability, PPX reproducibility, backend semantics and compatibility behavior are covered by automated regression/CI gates.

### Step 7 completion boundary

`0.7.0-dev.5` completes the planned Step 7 compiler architecture work. Promotion to a public 0.7 beta remains a separate release operation and still requires the real platform qualification jobs to pass. A failed Arch or Windows job is not converted into a success by finishing compiler development.

## Later releases

- **0.8:** ownership-aware structured async I/O, mature networking/stdlib, source debugging, safe cancellation/task groups, and broader platform validation.
- **0.9:** ecosystem/security hardening, fuzzing, compatibility suites, generated API docs/doctests, long-running benchmarks, and carefully reviewed metaprogramming/performance features.
- **1.0:** stable specification, compatibility guarantees, stable package/ABI policy, platform support tiers, and fully reproducible qualified releases.

Hosted registry operations, ARM/macOS, complete GUI infrastructure, inheritance, SIMD/PGO and full-language self-hosting remain outside the current Step 7 phase unless separately promoted with tests and design review.
