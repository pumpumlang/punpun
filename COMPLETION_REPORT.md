# PunPun 0.7.0-dev.1 engineering report

Date: 2026-09-08  
Version: `0.7.0-dev.1` (from repository `VERSION`)

This report records the first implemented portion of Step 7. It does not re-label unfinished platform release qualification or later Step 7 phases as complete.

## Baseline inherited from 0.6

Steps 1–6 remain implemented: version/release foundations, generics, algebraic enums and `Option`/`Result`, ownership/borrow analysis, verified HIR/MIR, optional LLVM, runtime/stdlib integration, self-host checks, tooling, PPX, documentation and packaging.

The `0.6.0-beta.1` cleanup candidate also added deterministic PP branding, source/release hygiene, normal push/PR CI, governance files, corrected package metadata and a publisher that blocks promotion until the exact tagged candidate passes required platform jobs.

The observed beta.1 Arch qualification attempt failed before makepkg/pacman qualification completed. That release remains blocked by design. 0.7 source development is allowed to continue without pretending beta.1 achieved platform qualification.

## Step 7 Phase 7.1 — implemented

### Verified Machine IR

A new `compiler/machine_ir.hpp` layer now sits below verified MIR and above code generation. It is target-aware and currently models `x86_64-sysv`.

Machine IR records:

- function and native symbol identity;
- PunPun internal vs SysV AMD64 calling convention;
- parameter/result ABI locations;
- hidden result pointers for by-value record returns;
- argument-block byte offsets and sizes;
- call barriers;
- liveness intervals;
- GPR/FPR/aggregate value classes;
- physical register or stack locations;
- callee-saved registers required by allocation metadata;
- logical spill slots and 16-byte-aligned frame requirements.

### Stronger register allocation

The Machine IR allocator is separate from the older MIR allocation display. It is call-clobber-aware:

- short-lived integer/pointer values can use caller-saved and preserved registers;
- values live across calls are restricted to preserved GPRs (`rbx`, `r12`–`r15`) or spills;
- call-live floating values spill because SysV XMM registers are caller-clobbered;
- aggregate values use stack storage;
- when register pressure is high, linear scan can evict a compatible active interval with a farther end in favor of a shorter-lived new value;
- spilled scalar values can reuse non-overlapping scalar spill slots.

### Machine IR verification

The verifier rejects invalid block targets, use-before-definition, missing locations, overlapping live register assignments, overlapping live spill ranges, call-live values in caller-clobbered registers, call-live FPRs in volatile XMM registers, out-of-frame spills, frame misalignment and ABI parameter-count drift.

### Backend consumption

The direct x86-64 backend now uses Machine IR as the function scheduling and ABI-layout authority. PunPun parameter offsets, internal argument-block sizes, hidden record result pointers and supported native extern argument registers are consumed from Machine IR rather than independently recomputed in those paths.

The portable C backend is scheduled from the authoritative Machine IR function set.

Detailed direct-x86 expression instruction selection still consults typed AST nodes in `0.7.0-dev.1`. Removing that remaining source-detail dependency is explicitly Phase 7.2, not falsely reported as complete here.

### Compiler tooling

`ppc emit-machine-ir <file.pp>` dumps target, function ABI, frame/spill information, machine locations, call barriers and allocation intervals.

Function body fingerprints now include Machine IR ABI/allocation identity, so target-layout changes invalidate build identity. Persisted function/module object reuse is still Phase 7.3.

### Tests

Regression tests cover:

- inspectable Machine IR target and ABI output;
- PunPun argument-block offsets;
- known call-site ABI annotations;
- call-live values avoiding caller-clobbered registers;
- high argument pressure producing verified stack spills;
- large argument-block sizing.

The complete existing suite is still required to pass before this snapshot is handed off.

## Incidental release-CI repair

The Arch workflow dependency list used the Debian-style package name `libcurl`. Arch provides the package as `curl`; Step 7 dev.1 corrects that setup line. This is a repair attempt, not a claim that the next Arch run has already passed.

## Remaining Step 7 work

- Phase 7.2: move remaining direct x86 body instruction selection behind Machine IR.
- Phase 7.3: persist dependency-aware function/module fingerprints and reuse independently compiled objects.
- Phase 7.4: Machine IR optimization, explicit spill/reload placement and allocation maturity.
- Phase 7.5: richer diagnostics/fix-its, module/API/FFI hardening and formatter/linter/PPX quality gates.

PunPun 0.7 is therefore **in development**, with the first architecture phase implemented and tested rather than the entire milestone being declared finished because a new header exists. The compiler has suffered enough human project-management traditions already.
