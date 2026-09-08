# Compiler architecture (0.6 beta)

The compiler is deliberately split into stages with one direction of authority:

```text
frontend.hpp      lexer/parser/AST + source spans
semantic.hpp      names, types, generics, contracts, enum/match typing
ownership.hpp     move-state and safe-borrow analysis
hir.hpp           typed control-flow/value IR
hir_opt.hpp       target-independent HIR optimization
mir.hpp           verified SSA-like MIR, liveness and virtual allocation
pipeline.hpp      mandatory HIR/MIR construction + stable fingerprints
backend_x86_64.hpp direct Linux x86-64 emission
backend_c.hpp     portable C17 lowering (also feeds optional LLVM)
main.cpp          CLI, cache, toolchain selection, build orchestration
```

Backends are not allowed to invent language semantics. The parser/semantic/ownership pipeline runs first, `pipeline.hpp` verifies HIR and MIR, and the MIR function set schedules backend emission. The 0.6 emitters still consult typed AST details for final lowering. 0.7 Machine IR will make instruction selection and ABI lowering fully IR-driven.

The LLVM option intentionally reuses `backend_c.hpp` as a stable portable lowering and asks Clang to produce LLVM/native code. It is an optional backend, not an alternate PunPun parser/type checker.
