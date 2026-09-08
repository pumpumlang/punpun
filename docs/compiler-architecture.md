# Compiler architecture (0.7 development)

PunPun keeps one direction of semantic authority. Backends do not re-parse or re-typecheck the language.

```text
frontend.hpp       lexer/parser/AST + source spans
semantic.hpp       names, types, generics, contracts, enum/match typing
ownership.hpp      move-state and safe-borrow analysis
hir.hpp            typed control-flow/value IR
hir_opt.hpp        target-independent HIR optimization
mir.hpp            verified target-independent SSA-like MIR
machine_ir.hpp     target ABI + call-aware liveness + physical allocation
pipeline.hpp       mandatory HIR/MIR/Machine-IR construction + fingerprints
backend_x86_64.hpp direct Linux x86-64 emission
backend_c.hpp      portable C17 lowering (also feeds optional LLVM)
main.cpp           CLI, cache, toolchain selection, build orchestration
```

## Authority chain

Every successful `check`, build, or backend emission runs:

```text
source
  -> parser
  -> semantic/type analysis
  -> ownership/borrow analysis
  -> typed HIR
  -> verified MIR
  -> verified Machine IR
  -> selected backend
```

HIR and MIR preserve language meaning without target calling-convention decisions. Machine IR is the first target-aware layer. It records the ABI and allocation facts that native code generation must obey.

## Machine IR in 0.7.0-dev.1

`compiler/machine_ir.hpp` introduces the first Step 7 Machine IR implementation for x86-64 SysV hosts. It contains:

- explicit PunPun internal argument-block ABI layouts;
- SysV AMD64 native-call register/stack placements;
- hidden result pointers for by-value record returns;
- call-barrier-aware liveness intervals;
- GPR/FPR/aggregate value classes;
- physical-register assignments;
- callee-saved placement for integer values that survive calls;
- spill slots and 16-byte-aligned frame requirements;
- verification that overlapping live values do not share a register or stack slot;
- verification that call-live values are not left in caller-clobbered registers.

Inspect it directly:

```sh
ppc emit-machine-ir main.pp
```

The dump is intended for compiler development and regression tests, not as a stable user-facing serialization format yet.

## Backend transition

The direct x86-64 backend now uses Machine IR as the function scheduling and ABI-layout authority. It no longer independently recomputes PunPun parameter offsets or record-return argument-block placement. Native extern argument registers are also read from Machine IR ABI metadata.

The detailed expression emitter still consults the typed AST in `0.7.0-dev.1`. That is an explicit transitional boundary, not a claim of full Machine-IR-only code generation. Step 7 Phase 7.2 moves the remaining instruction-selection details behind Machine IR.

The portable C backend is also scheduled from the authoritative Machine IR function set while continuing to use typed source details for C expression construction. This keeps C/LLVM as portability code-generation paths, not alternate semantic implementations.

## Fingerprints and incremental work

Function body fingerprints now include Machine IR ABI/allocation identity. This means ABI or allocation-relevant compiler changes invalidate build identity even when source text is unchanged. The next incremental-compilation phase persists per-function/module dependencies and reuses independently compiled native objects rather than only reusing a whole executable.

## LLVM

The optional LLVM path still reuses `backend_c.hpp` as a portable lowering and asks Clang to produce LLVM/native code. It goes through the same parser, semantic, ownership, HIR, MIR and Machine IR authority chain before backend emission.
