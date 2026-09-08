# PunPun 0.7.0-dev.1

`0.7.0-dev.1` starts Step 7, the compiler-scalability/backend-maturity cycle. This is a development snapshot, not a promoted stable/beta release.

## Step 7 Phase 7.1 highlights

- Added verified target-aware **Machine IR** below MIR.
- Added explicit PunPun argument-block and SysV AMD64 ABI metadata.
- Added byte-accurate parameter offsets and hidden destination pointers for by-value record returns.
- Added call-barrier-aware liveness and a stronger physical allocator.
- Call-live integer/pointer values prefer preserved registers; call-live floating values spill rather than silently surviving in volatile XMM registers.
- Added farthest-end linear-scan eviction and verified stack spill slots.
- Added Machine IR verification for control flow, definition/use order, allocation overlap, call-clobber safety and frame alignment.
- Added `ppc emit-machine-ir` for inspectable machine-level compiler state.
- Direct x86-64 code generation now consumes Machine IR function/ABI authority.
- Portable C code generation is scheduled from the Machine IR function set.
- Function fingerprints now include Machine IR ABI/allocation identity.
- Added regression tests for ABI layout, call-live register safety and register pressure.
- Corrected the Arch Actions setup package name from `libcurl` to `curl`.

## What is not complete yet

The direct x86 emitter still uses typed AST nodes for detailed expression instruction selection. Phase 7.2 moves that remaining lowering behind Machine IR. Function/module object-level incremental reuse, deeper Machine IR optimization, and diagnostics/tooling hardening are later Step 7 phases.

## Release qualification status

The previous `0.6.0-beta.1` publisher correctly stopped after an Arch qualification failure. This 0.7 development snapshot does not overwrite that fact or claim the failed job passed. Platform release promotion remains gated independently from source development.

## Inspecting Machine IR

```sh
ppc emit-machine-ir main.pp
```

The output includes the target, function calling convention, argument/result placement, frame/spill requirements, call barriers and allocation intervals. Its text form is a compiler-development aid and is not yet a stable serialization format.
