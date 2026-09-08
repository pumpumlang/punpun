# PunPun roadmap

PunPun is being developed toward Java-level reliability and C++-class native performance without hiding platform limitations or unfinished compiler work.

## 0.6 — language foundations

| Step | Scope | State |
| --- | --- | --- |
| 1 | Canonical versioning, green baseline, frozen generic/enum/nullability/ownership rules, parser scaffolding and compatibility gates | Complete in `0.6.0-dev.1` |
| 2 | Generic declarations/calls, constraints, monomorphization and deterministic specialization caching | Planned |
| 3 | Algebraic enums, `Option<T>`, `Result<T,E>`, destructuring and exhaustive matching | Planned |
| 4 | Full move-state analysis, implicit lexical `Drop`, safer borrows and slices | Planned |
| 5 | Make MIR authoritative for optimization and backend lowering; add interface/function fingerprints | Planned |
| 6 | Standard-library integration, release qualification and 0.6 compatibility report | Planned |

The normative 0.6 decisions are in [`spec/0.6/`](spec/0.6/). Syntax appearing there is not automatically implemented; the specification labels implementation gates explicitly.

## Later releases

- **0.7:** Machine IR, ABI lowering, real register allocation, deeper optimization and large-project incremental compilation.
- **0.8:** event-driven async I/O, mature networking/stdlib, source debugging and broader platform validation.
- **0.9:** ecosystem, fuzzing, compatibility suites, long-running benchmarks and production hardening.
- **1.0:** stable specification, compatibility guarantees and fully qualified releases.

Hosted registry operations, ARM/macOS, complete GUI infrastructure, inheritance, SIMD/LTO/PGO and full-language self-hosting remain outside 0.6 unless separately promoted with tests and design review.
