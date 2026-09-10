# PPC compiler roadmap

Ordered by measured value, not by how interesting the work is.

## After PunPun 1.3 — correctness, quality, performance

1. **Native register allocation** (linear scan). The largest remaining native
   gap after aggregate boxing. Target: `fib_recursive` from 2.5x C down toward
   1.5x, and a smaller artifact.
2. **Stack-passed arguments** in the native backend. Currently >6 integer or >8
   float parameters produce a deterministic E1000 diagnostic rather than wrong
   code, which is safe but is a real functional hole.
3. **Wire `analyze_parameters` into call sites**, removing the defensive copy
   for read-only value-struct parameters. The analysis already exists.
4. **Conservative mem2reg with an explicit Phi op**, done properly across the
   verifier, dump format, and all three backends — or remove `promoted_locals`
   and say so.
5. **Fuzzing and differential testing at scale.** The differential oracle
   (three backends, three optimization levels, same source) exists in the test
   runner but is driven by hand-written cases only.
6. Re-measure everything on a multi-core host where the noise floor permits a
   5% threshold to mean something.

## PunPun 1.4 — libraries and ecosystem

7. Semantic incremental compilation. The runtime-object cache is already
   content-addressed and hardened; module-level semantic fingerprinting is the
   next layer and is what makes large projects rebuild quickly.
8. Windows: maintain the platform layer and qualify it on a real Windows
   workflow. Tier 2 per `spec/1.0/platform-support.md`.
9. Optimization remarks and the performance map. Both are compile-time analysis
   with opt-in reporting, so they cost nothing when disabled.

## PunPun 1.5 — IDE and tooling

10. Extend the existing `LanguageService` and compiler-native LSP with
    workspace indexing, references, rename, signature help, code actions, and
    semantic highlighting for the dedicated PunPun IDE.
11. Structured fix suggestions carried in diagnostics, shared between the CLI
    and the editor so PunPun error logic exists exactly once.
12. Native debug information good enough to set a breakpoint on a `.pp` line.
13. PGO, last. It is only worth building once the native backend has enough
    optimizations for profile data to influence.
