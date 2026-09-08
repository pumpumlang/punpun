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

Step 7 is the 0.7 architecture milestone.

### Phase 7.1 — Machine IR and explicit ABI (`0.7.0-dev.1`) — complete

- Introduced verified target-aware **Machine IR** below MIR (`compiler/machine_ir.hpp`).
- Added explicit PunPun argument-block and SysV AMD64 ABI descriptions for parameters and return values.
- Added record-return hidden-result-pointer layout and byte-accurate PunPun argument-block offsets.
- Added call-barrier-aware liveness and a stronger linear-scan allocator with callee-saved register preference for call-live values, farthest-end eviction and spill slots.
- Added Machine IR allocation verification for overlapping registers/stack slots, invalid block targets, call-clobber violations and frame alignment.
- Direct x86-64 emission now consumes Machine IR as the function/ABI authority rather than recomputing PunPun call layouts independently.
- Portable C emission is scheduled from the authoritative Machine IR function set.
- Added `ppc emit-machine-ir <file.pp>` for inspectable ABI/allocation output.
- Function body fingerprints now include Machine IR ABI/allocation identity, preparing granular incremental compilation for later Step 7 phases.

### Phase 7.2 — complete backend decoupling — next

- Move remaining direct x86 instruction selection from typed AST/source-detail helpers into Machine IR operations.
- Make Machine IR the sole backend body representation for ordinary PunPun functions.
- Add explicit machine-level copies, loads/stores, call argument moves, spill/reload pseudos and lowered control-flow edges where required.
- Keep portable C/LLVM semantics on the same verified program authority while avoiding a second semantic implementation.

### Phase 7.3 — granular incremental compilation

- Persist function/module dependency fingerprints rather than only whole-program executable fingerprints.
- Rebuild only invalidated functions/modules and reuse stable native objects where safe.
- Surface precise cache reasons in `--cache-info` and rebuilt/reused counts in `--stats`.
- Keep toolchain, ABI, runtime, target and optimization configuration in cache identity.

### Phase 7.4 — optimizer and allocation maturity

- Add Machine IR copy propagation, dead-move elimination and branch cleanup with verifier checks between passes.
- Improve spill/reload placement and stack-slot reuse.
- Add call-aware register constraints and explicit callee-save emission once Machine IR drives final instruction emission.
- Expand correctness tests under high register pressure, loops, calls, records and async boundaries.

### Phase 7.5 — everyday compiler ergonomics

- Richer diagnostics and machine-applicable fix-its.
- Module/API visibility hardening and stable FFI/ABI documentation.
- Formatter/linter quality gates and reproducible PPX behavior.
- Benchmark and compatibility gates for large projects before 0.7 beta promotion.

## Later releases

- **0.8:** ownership-aware structured async I/O, mature networking/stdlib, source debugging, safe cancellation/task groups, and broader platform validation.
- **0.9:** ecosystem/security hardening, fuzzing, compatibility suites, generated API docs/doctests, long-running benchmarks, and carefully reviewed metaprogramming/performance features.
- **1.0:** stable specification, compatibility guarantees, stable package/ABI policy, platform support tiers, and fully reproducible qualified releases.

Hosted registry operations, ARM/macOS, complete GUI infrastructure, inheritance, SIMD/PGO and full-language self-hosting remain outside the current Step 7 phase unless separately promoted with tests and design review.
