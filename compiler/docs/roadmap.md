# PPC compiler roadmap

Ordered by measured value, not by how interesting the work is.

## After PunPun 1.3 — correctness, quality, performance

Completed in the staged native-backend pass:

- CFG-aware **linear-scan register allocation** for scalar MIR values using
  callee-saved x86-64 registers while retaining deterministic spill homes.
- Full scalar **System V stack-passed arguments**, direct and indirect, including
  mixed GP/SSE overflow.
- Native **async spawn/await/task-group coverage** through generated task
  wrappers and runtime trampolines.
- `analyze_parameters` wired into synchronous direct native calls to elide
  defensive deep copies of provably read-only value structs.

Remaining work:

1. **Conservative mem2reg with an explicit Phi op**, done properly across the
   verifier, dump format, and all three backends — or remove `promoted_locals`
   and say so.
2. **Fuzzing and differential testing at scale.** The differential oracle
   (three backends, three optimization levels, same source) exists in the test
   runner but is driven by hand-written cases only.
3. Re-measure everything on a multi-core host where the noise floor permits a
   5% threshold to mean something.

## PunPun 1.4 — libraries and ecosystem

4. Semantic incremental compilation. The runtime-object cache is already
   content-addressed and hardened; module-level semantic fingerprinting is the
   next layer and is what makes large projects rebuild quickly.
5. Windows: maintain the platform layer and qualify it on a real Windows
   workflow. Tier 2 per `spec/1.0/platform-support.md`.
6. Optimization remarks and the performance map. Both are compile-time analysis
   with opt-in reporting, so they cost nothing when disabled.

## PunPun 1.5 — IDE and tooling

7. Extend the existing `LanguageService` and compiler-native LSP with
    workspace indexing, references, rename, signature help, code actions, and
    semantic highlighting for the dedicated PunPun IDE.
8. Structured fix suggestions carried in diagnostics, shared between the CLI
    and the editor so PunPun error logic exists exactly once.
9. Native debug information good enough to set a breakpoint on a `.pp` line.
10. PGO, last. It is only worth building once the native backend has enough
    optimizations for profile data to influence.
