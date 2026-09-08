# PunPun 0.6.0-beta.1 engineering report

Date: 2026-09-08  
Version: `0.6.0-beta.1` (from repository `VERSION`)

This report is the local release gate for PunPun 0.6. It distinguishes features actually implemented and executed on the Linux x86-64 release host from platform claims that require Windows or Arch/CachyOS.


## Pre-Step-7 cleanup gate

The beta.1 candidate is a release-integrity pass, not Step 7 compiler work. It makes PP branding single-source, blocks timestamped backups/debris, removes placeholder package metadata, adds continuous CI/link checks/security guidance, hardens platform setup, and prevents release promotion until the exact candidate commit passes Linux, Arch and Windows qualification.

## Steps completed

### Step 1 — release/version foundation
One canonical version source, generated mirror checks, frozen 0.6 specifications, parser scaffolding, compatibility fixtures and release-truthfulness gates.

### Step 2 — generics
Generic functions/types/methods, inference and explicit type arguments, constraints, deterministic mangling, bounded specialization and monomorphization deduplication.

### Step 3 — algebraic data types
Algebraic enums, payloads, nested patterns, exhaustiveness/reachability diagnostics, `Option<T>`, `Result<T,E>` and postfix `?` across both primary backends.

### Step 4 — ownership, borrows and slices
A separate post-typechecking ownership pass tracks initialized/moved/maybe-moved state across control-flow joins, rejects conflicting safe borrows, supports explicit move/drop and reinitialization, inserts lexical destruction for supported identity objects, and checks non-owning `Slice<int>` views tied to `nums` owners. Legacy `nums` assignment/parameter semantics remain shared-handle compatible with 0.5.

### Step 5 — mandatory HIR/MIR pipeline
Every successful check/build/backend emission constructs typed HIR, optionally optimizes HIR, verifies it, lowers verified MIR, verifies MIR and computes deterministic interface/body/program fingerprints. Backends are scheduled by the authoritative MIR function set. Final 0.6 instruction lowering may still consult typed source-node details; removing that final dependency belongs to 0.7 Machine IR.

### Step 5.5 — optional LLVM
`--llvm-backend` and `emit-llvm` add an optional Clang/LLVM path after the same PunPun frontend, semantic, ownership, HIR and MIR stages. This does not turn ordinary PunPun into a C/C++ transpiler and does not create a second semantic implementation.

### Step 6 — integration and release qualification
The language/runtime tests, self-host bootstrap, editor/LSP, PPX, static sites, isolated installer validation, privacy audit, version consistency and artifact validation are part of the release workflow. Release artifacts are produced only after those host-available gates pass.

## Intentional 0.6 boundaries

- Custom user-defined destructors, field-level partial moves, lifetime parameter syntax, owned generic collections and allocator/arena APIs remain later work.
- MIR is mandatory and authoritative for verified program/function identity, but 0.7 will add Machine IR and remove remaining source-detail dependencies from backend instruction selection.
- The LLVM compatibility backend is optional and native-host only in 0.6.
- Full-language self-hosting is not claimed; the PunPun-written bootstrap compiler is a verified fixed-point seed.
- Windows MSI/setup execution and real Arch/CachyOS `pacman` install/upgrade/remove cannot be certified on this Linux host.
- ARM64/macOS, direct PE/COFF emission, PDBs, advanced devirtualization/escape analysis/vectorization/PGO and a production async reactor remain later milestones.

These are release boundaries, not disguised successes. The beta contains tests and architecture intended to make the next compiler generation possible without restarting the project.
