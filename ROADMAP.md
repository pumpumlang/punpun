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
- **Optional LLVM:** `--llvm-backend` uses Clang/LLVM after the same PunPun parser, semantic, ownership, HIR and MIR pipeline. `emit-llvm` exposes generated LLVM IR. In 0.6 this path is native-host only.

LLVM is an alternative code-generation path, not a replacement parser/type checker and not the ordinary PunPun compilation pipeline.

## Pre-Step-7 quality gate

`0.6.0-beta.1` is the cleanup/qualification candidate between Step 6 and Step 7. It canonicalizes the PP brand, rejects source/release debris, fixes packaging metadata, adds continuous CI and governance files, repairs platform setup, and changes publication to **build → qualify → promote**. Step 7 does not begin until the candidate's Linux, Arch and Windows qualification jobs are green.

## Step 7 / PunPun 0.7 — compiler scalability and backend maturity

Step 7 is now formally defined as the 0.7 architecture milestone:

1. introduce Machine IR below verified MIR;
2. define explicit ABI lowering into Machine IR;
3. make native backends consume Machine IR rather than source-detail escape hatches;
4. strengthen register allocation and spill handling;
5. deepen optimization with correctness-first verification;
6. add function/module-granular incremental compilation with dependency fingerprints;
7. improve diagnostics/fix-its, module/API visibility, FFI/ABI documentation, formatter/linter gates, and PPX reproducibility without destabilizing core semantics.

The acceptance gate for Step 7 includes deterministic brand generation, debris-free source archives, green fast CI, green Linux/Arch/Windows qualification, exact release provenance, and publication blocked on failed qualification.

## Later releases

- **0.8:** ownership-aware structured async I/O, mature networking/stdlib, source debugging, safe cancellation/task groups, and broader platform validation.
- **0.9:** ecosystem/security hardening, fuzzing, compatibility suites, generated API docs/doctests, long-running benchmarks, and carefully reviewed metaprogramming/performance features.
- **1.0:** stable specification, compatibility guarantees, stable package/ABI policy, platform support tiers, and fully reproducible qualified releases.

Hosted registry operations, ARM/macOS, complete GUI infrastructure, inheritance, SIMD/PGO and full-language self-hosting remain outside 0.6 unless separately promoted with tests and design review.
