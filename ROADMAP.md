# PunPun roadmap

PunPun is developed as a stable 1.x language with measurable compiler behavior.
Platform support is promoted only after the corresponding real workflow passes.

## Completed

- **1.0 — stable contract:** language and runtime ABI epoch 1, frozen public API, deterministic release metadata, PPX integrity, and platform support tiers.
- **1.3 — compiler integration and stability:** PPC replaces the legacy compiler; C, native x86-64, and bytecode share one frontend; compiler-native LSP; expanded standard library; verified HTTPS and native GUI foundations.

## 1.4 — libraries and ecosystem

- Expand HTTP ergonomics with structured response/header values and reusable clients while retaining the 1.3 primitives.
- Build out GUI widgets, layout, events, and lifecycle above the native 1.3 window foundation.
- Add focused database, serialization, process, and networking libraries with backend-equivalence tests.
- Improve PPX discovery, package documentation, and compatibility metadata.
- Add module-level semantic incremental compilation after correctness fingerprints are specified.

## 1.5 — IDE and tooling

- Build the dedicated PunPun editor/IDE on the compiler's `LanguageService`.
- Extend the shared language service with references, rename, signature help, code actions, semantic highlighting, and workspace indexing.
- Add first-class PunPun, C, and C++ project workflows without duplicating language semantics in the editor.
- Improve source-level debugging and native optimization reporting.

## Compiler work that remains explicit

- Native x86-64 register allocation and stack-passed arguments.
- Broader native async coverage.
- Conservative interprocedural copy elimination.
- ARM64/macOS ports and real Windows runtime qualification.
- True nonblocking networking; current async HTTPS wrappers use worker tasks around blocking libcurl calls.

A feature is complete only when its language/runtime implementation, tests,
documentation, compatibility impact, and applicable platform gates agree.
