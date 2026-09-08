# PunPun 0.6 development — Step 1

This development checkpoint establishes the rules and release infrastructure required for the 0.6 language work without advertising unimplemented features as complete.

## Highlights

- A single root `VERSION` now controls compiler output, runtime compatibility, PPX, editor packages, sites, Linux/Arch packaging and Windows installer names.
- Normative specifications now define generics, monomorphization, constraints, algebraic enums, `Option`, `Result`, exhaustive matching, nullability, move-state analysis, borrowing and deterministic `Drop`.
- The parser accepts generic declaration headers and nested generic type spellings for AST/tooling work.
- Reserved enum, match and propagation syntax fails with an explicit implementation-gate diagnostic.
- Existing valid 0.5 modern and migration-dialect programs remain under compatibility tests.
- CI and publishing use version-derived paths and anonymous runtime account discovery.

## Honest boundary

This is not the completed 0.6 language release. Generic execution and monomorphization are Step 2; algebraic enums and matching are Step 3; full ownership/drop insertion and MIR authority follow afterward.

The Linux artifacts can be built and validated on Linux. Arch package-manager operations and Windows MSI/setup behavior must be validated by their real platform jobs before those artifacts are claimed as qualified.

See `ROADMAP.md`, `spec/0.6/` and `COMPLETION_REPORT.md` in the source archive.
