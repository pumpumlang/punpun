# PunPun 0.6 development — Steps 2 and 3

This development checkpoint establishes the rules and release infrastructure required for the 0.6 language work without advertising unimplemented features as complete.

## Highlights

- A single root `VERSION` now controls compiler output, runtime compatibility, PPX, editor packages, sites, Linux/Arch packaging and Windows installer names.
- Normative specifications now define generics, monomorphization, constraints, algebraic enums, `Option`, `Result`, exhaustive matching, nullability, move-state analysis, borrowing and deterministic `Drop`.
- Generic functions and types now execute through deterministic monomorphization with inferred/explicit arguments, nested constructed types, constraints, generic methods and specialization deduplication.
- Algebraic enums, tuple payloads, nested destructuring, reachability/exhaustiveness diagnostics, prelude `Option<T>`/`Result<T,E>`, and postfix `?` now execute through both Linux backends.
- `std::option` and `std::result` provide generic query and fallback helpers, with a complete runnable example.
- Existing valid 0.5 modern and migration-dialect programs remain under compatibility tests.
- CI and publishing use version-derived paths and anonymous runtime account discovery.

## Honest boundary

This is not the completed 0.6 language release. Steps 2 and 3 are implemented and regression-tested; full ownership/drop insertion and MIR authority follow in Steps 4 and 5.

The Linux artifacts can be built and validated on Linux. Arch package-manager operations and Windows MSI/setup behavior must be validated by their real platform jobs before those artifacts are claimed as qualified.

See `ROADMAP.md`, `spec/0.6/` and `COMPLETION_REPORT.md` in the source archive.
