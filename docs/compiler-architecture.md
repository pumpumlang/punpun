# Compiler architecture (0.9 development)

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

## Machine IR in 0.7.0-dev.5

`compiler/machine_ir.hpp` is the target-aware contract between verified MIR and native code generation. It contains:

- explicit PunPun internal argument-block ABI layouts;
- modeled SysV AMD64 native-call register/stack placements;
- hidden result pointers for by-value record returns;
- call-barrier-aware liveness and GPR/FPR/aggregate classes;
- allocator-owned register homes and spill ranges;
- explicit callee-saved requirements;
- CFG-aware conservative spill handling for values crossing block boundaries;
- 16-byte-aligned frame requirements and allocation/control-flow verification;
- local propagation/cleanup, constant branch simplification and unreachable block pruning.

Inspect target allocation and ABI state directly:

```sh
ppc emit-machine-ir main.pp
ppc emit-abi main.pp
```

The textual dump is a compiler/debugging format, not yet a stable serialization API.

## Backend authority after Step 7

Every non-extern PunPun function in the direct x86-64 backend is emitted by walking verified Machine IR blocks/instructions. The earlier typed-source function-body emitter has been removed. Machine IR explicitly carries the operations needed for lexical moves/drops, aggregates/enums, member/address/deref/index mutation, lists, short-circuit CFG and async/await behavior.

The direct backend also consumes Machine IR parameter offsets, internal argument-block sizes, hidden record-result pointers, native argument locations, physical register assignments, spill ranges and callee-save requirements. It does not rebuild those facts from source-level syntax.

The portable C backend remains a portability lowering scheduled from the same authoritative compiled function set. The optional LLVM path continues to use portable C/Clang after the shared frontend/analysis/IR authority chain; it is not an alternate parser/type checker.

## Granular incremental compilation

For direct-native builds, the driver emits one independently assemblable object per PunPun function plus independently cached process-entry glue. Each function cache key includes target/toolchain/optimization configuration and three compiler fingerprints:

1. **interface** — callable type/interface identity;
2. **dependency** — direct called-function interface hashes plus reachable shape/enum layout identity;
3. **body** — Machine-IR body, debug mapping and allocation identity.

This allows unrelated functions to remain cache hits when another function changes. `--cache-info` reports function-level reasons and `--stats` reports reused/rebuilt function and module counts. `scripts/benchmark_projects.py --gate` is the CI scalability contract for the generated project workload.

## Machine IR optimizer/allocation boundary

The current allocator deliberately prefers correctness over unsafe cross-CFG coalescing. Straight-line scalar temporaries can live in physical registers and non-overlapping spills can reuse stack slots. Call-live values use preserved registers or spills. Values whose SSA lifetime crosses basic-block boundaries use dedicated spill ranges until a future interference-graph allocator can prove safe coalescing across joins/loops.

That conservative rule is intentional: block storage order is not dominance order, so ordinary linear interval overlap is insufficient at match/branch joins. The verifier continues to reject overlapping final register/stack locations and call-clobber violations.

## LLVM

The optional LLVM path still reuses `backend_c.hpp` as a portable lowering and asks Clang to produce LLVM/native code. It goes through the same parser, semantic, ownership, HIR, MIR and Machine IR authority chain before backend emission.


## Step 8/9 runtime and quality surfaces

Step 8 does not insert a second compiler IR layer. Structured task groups are runtime operations surfaced as typed built-ins; `async fn`/`await` continue through the normal frontend/HIR/MIR/Machine-IR pipeline. Cancellation is cooperative, with runtime safe points such as `sleep_ms`.

Source debugging reuses direct-backend assembler `.file`/`.loc` directives. `scripts/debug_map.py` converts those mappings into deterministic JSON, while `pp debug` delegates interactive debugging to GDB/LLDB.

Step 9 production gates sit around the compiler rather than changing language semantics: generated API docs/doctests, deterministic frontend mutation fuzzing, backend compatibility execution, structured-async stress and portable-C PGO. PPX package manifests add archive-content integrity without changing the compiler's trust model or claiming publisher-signature authentication.
