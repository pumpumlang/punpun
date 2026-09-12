# PunPun roadmap

PunPun is developed as a stable 1.x language with measurable compiler behavior.
Platform support is promoted only after the corresponding real workflow passes.

## Completed

- **1.0 — stable contract:** language and runtime ABI epoch 1, frozen public API, deterministic release metadata, PPX integrity, and platform support tiers.
- **1.3 — compiler integration and stability:** PPC replaces the legacy compiler; C, native x86-64, and bytecode share one frontend; compiler-native LSP; expanded standard library; verified HTTPS and native GUI foundations.

## 1.4 — libraries and ecosystem

- Structured HTTP/1.1 request/response values, reusable clients, server helpers, binary bodies and verified HTTPS are implemented.
- DNS, nonblocking TCP/UDP runtime sockets and `ws://` WebSockets are implemented with C/native/bytecode equivalence tests.
- Retained GUI widgets, layouts, event/lifecycle handling, headless testing and canvas drawing are implemented on the Win32/X11 foundation.
- JSON/TOML serialization, captured processes, pure-PunPun compression/ZIP support, and a focused atomic key/value database are implemented with backend-equivalence tests.
- Improve PPX discovery, package documentation, and compatibility metadata.
- Add module-level semantic incremental compilation after correctness fingerprints are specified.

## 1.5 — IDE and tooling

- Build the dedicated PunPun editor/IDE on the compiler's `LanguageService`.
- Extend the shared language service with references, rename, signature help, code actions, semantic highlighting, and workspace indexing.
- Add first-class PunPun, C, and C++ project workflows without duplicating language semantics in the editor.
- Improve source-level debugging and native optimization reporting.

## 1.5.0 halfway milestone

- Capturing closures.
- General iterator protocol.
- Native x86-64 backend improvements.
- DNS/TCP/UDP/HTTP/WebSocket networking stack.
- Retained GUI toolkit.
- Broad PunPun-written standard-library expansion.

## 1.5.5 completion target

- Cross-platform native code generation.
- Expanded LSP/editor intelligence.
- Source debugger and profiler.
- Module-level semantic incremental compilation.
- PPX/package ecosystem improvements.
- Stronger C/native FFI and binding tooling.

## Compiler work that remains explicit

- Extend read-only value-parameter copy elimination from native x86-64 to the
  bytecode backend.
- Conservative mem2reg/phi construction for locals that never escape.
- ARM64/macOS ports and real Windows runtime qualification.
- Raw reviewed TLS streams for `wss://` and true nonblocking HTTPS; current HTTPS async wrappers still use worker tasks around blocking libcurl calls.

A feature is complete only when its language/runtime implementation, tests,
documentation, compatibility impact, and applicable platform gates agree.
