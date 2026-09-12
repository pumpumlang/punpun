# Changelog

## Unreleased

## 1.5.0 — 2026-09-11

### Standard library expansion

- Replaced the first-party JSON package's injected-C parser with a recursive
  `std.data.json` value model, parser, serializer, pretty-printer, Unicode escape
  handling and typed object/array access written in PunPun.
- Added PunPun-native TOML/config parsing, a backtracking regular-expression
  engine, UTF-8 codepoint helpers, MIME lookup, deterministic RNG utilities,
  date/time values, path logic, filesystem tree/atomic-write helpers and
  structured logging/testing APIs.
- Added pure-PunPun SHA-256 and HMAC-SHA256 implementations validated against
  published vectors, plus LZSS/RLE byte compression.
- Added interoperable method-0 ZIP archive reading/writing with CRC-32 checking
  and a small atomic JSON-backed document/key-value database.
- Added generic higher-order collection functions (`map`, `filter`, `fold`,
  predicates, `zip`, `enumerate`) and a generic deque. This exposed and fixed
  missing generic substitution inside `fn(T) -> U` types.
- Added captured process results and host/environment helpers. Only the actual
  platform operations are runtime shims; argument quoting, result modelling and
  application policy live in PunPun.
- Upgraded the `json`, `logging`, `filesystem` and `testing` first-party packages
  to reuse the full standard modules, with package smoke tests on all backends.
- Fixed bytecode dispatch for the new host primitives, native string equality's
  SysV `bool` widening, and native escape/copy-elision of stack-promoted value
  structs stored into containers, all exposed by the new PunPun-written library
  tests.

### GUI toolkit

- Replaced the two-call native GUI foundation with a retained `std.gui` toolkit
  covering windows, labels, buttons, text inputs, checkboxes, sliders, progress
  bars, panels and canvases.
- Added portable widget bounds/text/value/range/visible/enabled state plus
  vertical, horizontal and grid layouts implemented in PunPun itself.
- Added mouse, keyboard, text, change, resize, paint and close events, an
  application event queue, and closure-driven `gui_run` event loops.
- Added RGB canvas clear/rectangle/line/text drawing and modal confirm helpers.
- Added `PUNPUN_GUI_HEADLESS=1`, which runs the same retained model without a
  display so GUI programs can be regression-tested across C, bytecode and native.
- Expanded the X11 backend with real input focus, slider interaction, WM close
  handling and display cleanup; expanded Win32 linking/painting for GDI-backed
  toolkit controls.
- Fixed native x86-64 runtime-builtin calls to spill arguments beyond the six
  System V GP registers, exposed by the seven-argument canvas rectangle call.


### Networking stack

- Added backend-equivalent DNS, TCP and UDP runtime sockets with portable
  runtime handles, nonblocking operation, timeout/readiness waits and task
  cancellation awareness. Windows uses Winsock through the same public API.
- Added `std.net.dns`, `std.net.tcp` and `std.net.udp`, including listeners,
  peer/local addressing, `TCP_NODELAY`, half/full shutdown, async wrappers and
  binary datagrams with source addresses.
- Added structured `std.net.http` HTTP/1.1 client/server support with header
  maps, binary request/response bodies, closure handlers, content-length and
  chunked decoding. Plain HTTP uses PunPun sockets directly.
- Extended the verified libcurl HTTPS runtime with raw response headers and
  binary request/response bodies so HTTP and HTTPS share one high-level
  `HttpResponse` shape without weakening certificate or hostname verification.
- Added RFC 6455 `ws://` WebSockets with client masking, text/binary messages,
  fragmentation, ping/pong, close frames and a 16 MiB message cap.
- Raw socket operations are exercised on C, bytecode and direct x86-64. Raw TLS
  streams/`wss://` and HTTP keep-alive pooling remain explicit future work;
  HTTPS async wrappers still run blocking libcurl inside cancellable worker
  tasks rather than pretending libcurl is a nonblocking socket backend.

### Native x86-64 backend

- Added CFG-aware linear-scan allocation for scalar MIR values across `%rbx`
  and `%r12`–`%r15`, with conservative spill homes retained for exact
  materialization and debugging.
- Added full scalar System V stack argument passing for direct and indirect
  calls, including mixed integer/pointer and floating-point overflow past the
  six GP and eight SSE argument registers.
- Added native async task spawning, worker trampolines, `await`, cancellation,
  and task-group parity with the C backend.
- Wired read-only parameter analysis into synchronous direct native calls so
  provably read-only value structs avoid a defensive deep copy. Async and
  indirect calls remain conservative.
- Added native codegen structural checks plus wide-call and async ABI regression
  cases; the three backends continue to share the same behavioral suite.

### Capturing closures

- Function literals capture free locals by value into an owned environment.
- Mutable captures persist across calls; copying a function value shares the
  same environment, while the creating scope keeps its own copy of Copy values.
- Closures may escape their creating function and nested closures propagate
  grandparent captures through intermediate environments.
- Capturing move-only values transfers ownership into the closure.
- Capturing `&T`, `&mut T`, or `Slice<T>` is rejected until lifetime-aware
  closure escape analysis can prove the borrow cannot dangle.
- C, direct x86-64, and bytecode backends use the same one-word closure handle
  model and are regression-tested for equivalent behavior.


### General iterator protocol

- `for element in value` now accepts user-defined iterables structurally: an
  iterable exposes `iter()`, whose result exposes `advance() -> Option<T>`.
- Iterator objects may be looped directly when they expose `advance() ->
  Option<T>`, enabling lazy and unbounded producers without materializing a
  sequence first.
- Generic concrete iterator types propagate their owner type arguments through
  the protocol, so `Option<T>` determines the loop variable type.
- Existing `nums`, `List<T>` and `Slice<T>` loops retain their indexed fast
  path; the protocol adds no allocation or dispatch overhead to those types.
- Stateful protocol iterators are identity `object`s. A `struct` iterator whose
  `advance` requires `mut self` is rejected until mutable value-struct borrows
  have the same representation on every backend.
- C, direct x86-64 and bytecode run the same protocol regression cases,
  including direct iterators, generic iterables, `break`, and `continue`.

## 1.4.5 — 2026-09-11

- Coordinated the language, documentation, and PPX package manager on the
  `1.4.5` stable release number.
- Fixed release packaging to embed the exact matching PPX client in Linux SDK,
  self-extracting installer, Arch/CachyOS package, and Windows payloads.
- Made `pp build`, `pp run`, `pp check`, and emit commands automatically pass
  materialized PPX dependency roots to the compiler.
- Added release tests that reject missing or mismatched PPX companion sources.
- Corrected stale 1.3 labels in the live example and first-party package docs.

## 1.4.0 — 2026-09-11

Three language gaps closed. Every addition is source-compatible with 1.0: the
language version, ABI, package format and lockfile format all stay put.

### Functions are values

- `fn(T, U) -> R` is a type, usable for parameters, locals and results.
- A function named without parentheses is a value. An expected function type
  picks between overloads; a generic function is refused, having no single
  address until its type arguments are given.
- `fn(x: int) -> int { ... }` may be written in expression position. It is
  lifted to a module-level function at parse time, so nothing after the parser
  needs a second notion of what a function is.
- A function value is the callee's index in the module function table: one
  word, and the reason the three backends share one calling sequence.
- Literals do not capture. Naming a local from around one is refused with an
  error saying exactly that, rather than claiming the name does not exist.
- `sort_by` joins the standard library — the comparator that could not be
  written before.

### Sequences iterate

- `for element in sequence` over nums, List<T> and Slice<T>, including lists of
  aggregates. It is rewritten in the checker into the indexed loop it replaces,
  so all three backends gained it without change.

### Contracts are types

- `fn draw(s: Shape)` is accepted, and `List<Shape>` holds values of several
  concrete types at once.
- An object carries its type identity in a hidden leading field, which is what
  lets a contract value be the bare handle and therefore fit a List slot.
- Dispatch compares that identity against the types declaring they meet the
  contract, expanded inline.

### One grammar

- The migration dialect warns, as W2000, naming the modern spelling and
  pointing at `pp migrate`. It still parses: 1.x promised valid 0.6 source
  keeps building, and removing the forms is a major-version decision.
- Warning codes render with a W prefix. Every code printed as E before, so a
  warning quoted alone looked like an error.
- The repository's own examples were converted, having been split between the
  two grammars. Each produces identical output.
- `pp migrate` left `keep name as Type = value` half-converted, dropping only
  the arrow; it handles the annotated form now.

### Still missing

- Function literals cannot capture.
- Contract dispatch is a comparison chain, not a table; it is linear in the
  number of implementors at each call site.
- `for` does not walk Map<V> or the characters of a str.
- Windows has still never been compiled; macOS has never been tested.

### Repository

- Split the project across three repositories. This one is now the language
  alone: compiler, runtime, standard library, first-party packages,
  specification and editor integration.
- Moved the documentation site, its Markdown sources and the long-form
  reference to `punpun-docs`, which now builds and publishes itself.
- Moved the PPX client, the reference registry and the catalog site to
  `punpun-ppx`, which now builds and publishes itself.
- Removed the desktop IDE, the GUI designer and the IDE environment probe; they
  are separate products rather than part of the language.
- Removed the Python test suite that targeted the replaced pre-1.3 compiler.
  Nine of its fifteen files failed and one ran no tests at all. The surviving
  desktop-integration, publish-cleanup and release-hygiene tests now run as
  part of `make test`, which nothing had been doing.
- `pp add`, `remove`, `tree`, `update` and `fetch` now resolve PPX through
  `PUNPUN_PPX`, `PATH` or a sibling checkout, and explain how to install it
  when it is absent.
- `pp test --doc` now runs doctests over the current project instead of the
  toolchain's own documentation tree.
- Stopped publishing the documentation and catalog sites from this repository's
  release script, which had been overwriting both companion repositories.
- Moved the 1.3.0 entry below to the top of this file, where it belongs, and
  added the missing 1.0.0 entry.

## 1.3.0 — 2026-09-10

- Replaced the legacy compiler with the supplied PPC C++20 compiler, preserving language/runtime ABI epoch 1.
- Integrated portable C, direct x86-64, and bytecode backends plus the compiler-native LSP.
- Added verified HTTPS builtins, `std.net.https`, a first-party `https` package, and a compatibility-preserving `requests` implementation.
- Added native GUI availability/message APIs backed by Win32 or dynamically loaded X11/XWayland.
- Expanded the standard library and generated API reference.
- Updated the launcher, VS Code extension, self-host bootstrap, CI/release tooling, and platform documentation for the canonical compiler.

## 1.0.0 — 2026-09-09

- Froze the first stable compatibility line: language/runtime ABI epoch 1 and package/lockfile format 1.
- Added a machine-enforced public language and standard-library baseline, checked by `pp stable-check`.
- Added `pp platform-info` and explicit platform support tiers.
- Added Ed25519-capable release and PPX verification, plus source SBOM and provenance generation.

## 0.9.0-dev.5 — 2026-09-08

- Completed Step 9 ecosystem/security/production hardening on the Linux x86-64 development host.
- Added deterministic PPX per-file integrity manifests, `ppx verify`, HTTPS-by-default registry policy and dependency `ppx audit` injection checks.
- Added generated stdlib/first-party API documentation, `pp doc --check`, explicit PunPun doctests and `pp test --doc`.
- Added deterministic frontend mutation fuzzing, direct/C/LLVM compatibility checks and repeated structured-async stress testing.
- Added portable-C profile-guided optimization through `pp pgo`; direct-x86 PGO is deliberately not claimed.
- Integrated the new documentation, fuzz, compatibility and stress gates into Makefile and CI/release qualification.

## 0.8.0-dev.5 — 2026-09-08

- Completed Step 8 structured async/networking/debugging work on the Linux x86-64 development host.
- Added runtime task groups with wait, timed wait, pending/done, cancellation and deterministic close operations.
- Made sleep a cooperative cancellation safe point and added regression coverage for prompt cancellation wake-up.
- Added async stdlib file helpers and first-party requests async wrappers that compose with task groups.
- Added deterministic source debug maps and GDB/LLDB launch support through `pp debug-map` / `pp debug`.
- Reworked Arch release qualification to run package operations in an explicit current Arch Docker environment while keeping platform success dependent on a real green workflow.

## 0.7.0-dev.5 — 2026-09-08

- Completed all planned Step 7 / 0.7 compiler architecture phases on the Linux x86-64 development host.
- Removed the direct-x86 typed-source body emitter; every PunPun native function body now lowers from verified Machine IR.
- Added canonical lexical storage identities plus explicit move/drop, aggregate/enum, member/address/index, list and await Machine-IR semantics.
- Added CFG-safe allocation behavior: cross-block values receive conservative dedicated spill ranges while straight-line spills can still reuse non-overlapping slots.
- Made native emission consume allocator physical register homes/spill ranges and emit required callee-save preservation.
- Added Machine-IR propagation/cleanup, branch simplification, unreachable-block pruning and verifier checks around optimization.
- Added dependency-aware per-function object caching with interface/body/direct-layout dependency hashes and precise hit/miss reasons.
- Added exact incremental benchmark gating: a one-function edit must rebuild exactly one function in the generated project workload.
- Added machine-applicable unknown-name fix-it coverage, module-private function enforcement and duplicate-import linting.
- Made PPX publish archives byte reproducible across mtime-only source changes.
- Added explicit 0.7 incremental-compilation and FFI/ABI specifications.
- Kept Arch/Windows release qualification separate; no platform success is claimed without a successful real platform workflow.

## 0.7.0-dev.2 — 2026-09-08

- Continued Step 7 Phase 7.2 with a direct-x86 body emitter driven by verified Machine IR for eligible scalar/control-flow functions.
- Machine-IR body lowering now covers scalar constants/locals, integer/float/string operations, comparisons, branches, loops, `say`, returns, and ordinary PunPun calls.
- PunPun call argument blocks in the new path are populated from Machine IR ABI metadata rather than source-level call reconstruction.
- Added conservative backend selection: short-circuit logic and operations whose ownership/aggregate semantics are not yet explicit in IR stay on the legacy source-detail emitter.
- Assembly now annotates each function with its body-lowering path for regression tests and compiler debugging.
- Preserved the minimum signed 64-bit literal special case while moving unary lowering behind Machine IR.
- Added tests for Machine-IR scalar CFG/calls and safe short-circuit fallback.
- Kept the failed beta.1 Arch qualification as a separate release issue rather than weakening the publish gate.

## 0.7.0-dev.1 — 2026-09-08

- Started Step 7 with a verified target-aware Machine IR below MIR.
- Added explicit PunPun argument-block and SysV AMD64 ABI descriptions.
- Added record hidden-result-pointer layout and byte-accurate parameter offsets.
- Added call-barrier-aware liveness and stronger linear-scan allocation with callee-saved placement, farthest-end eviction and spill slots.
- Added Machine IR allocation/control-flow verification and `ppc emit-machine-ir`.
- Direct x86-64 lowering now consumes Machine IR function/ABI authority; portable C emission is scheduled from the Machine IR function set.
- Function body fingerprints now include Machine IR ABI/allocation identity as groundwork for granular incremental compilation.
- Added Step 7 Machine IR specification and regression tests for ABI layout, call-live register safety and register-pressure spills.
- Corrected the Arch workflow dependency from the Debian-style `libcurl` package name to Arch's `curl` package.
- Kept beta.1 release promotion gated: a failed platform qualification remains a release blocker, not a fake success.

## 0.6.0-beta.1 — 2026-09-08

- Canonicalized the new PP brand so Linux MIME icons, VS Code marketplace/file icons, favicons, social artwork and Windows ICOs are generated from one source.
- Removed release-hygiene gaps for timestamped `.bak-*`, `.orig`, `.rej`, swap and temporary files.
- Replaced placeholder Arch package URLs with the canonical PunPun repository URL.
- Added continuous push/PR CI, documentation link validation, security/contribution guidance and issue templates.
- Updated GitHub Actions to Node 24-capable releases and hardened Arch/Windows qualification setup.
- Changed publishing to build/qualify/promote: release assets and sites are blocked until the exact candidate commit passes Linux, Arch and Windows qualification.
- Formally defined Step 7 as the PunPun 0.7 compiler-scalability/backend-maturity milestone.

## 0.6.0-beta — 2026-09-08

- Reworked the documentation site into a staged beginner → intermediate learning path with compiler-checked runnable examples, common-mistake guidance, grouped navigation, and next-page progression.
- Added `.pp` file icon integration for VS Code language icons, Linux `application/x-punpun` MIME/hicolor icons, and a real Windows multi-resolution ICO association.
- Extended PPX with account registration, hidden-password login, `publish`/`upload --dry-run`, validated publish metadata/dependencies, checksum-verified `download`, and named `install`.
- Rebuilt the main README around installation, quickstart, language/tooling examples, PPX authoring, documentation, and contribution workflow.
- Hardened source/repository publishing cleanup and stale release-asset pruning with explicit, bounded cleanup rules.
- Completed Steps 4–6: move-state/borrow analysis, checked slices, lexical object destruction, mandatory verified HIR/MIR, deterministic compiler fingerprints, release qualification and compatibility gates.
- Added optional `--llvm-backend` and `emit-llvm` using Clang/LLVM after the shared PunPun frontend/MIR pipeline.
- Preserved 0.5 `nums` shared-handle behavior while keeping explicit `move(nums)` available for binding-lifetime transfer.
- Fixed Step 3 portable-backend match warnings, method-receiver ownership, minimum-int HIR typing, and regressions exposed by mandatory MIR verification.

## PunPun 0.6 development — Steps 2 and 3

- Added generic function, struct, object and method execution with explicit/inferred type arguments.
- Added deterministic, deduplicated monomorphization and specialization work limits.
- Added compiler-known `Copy`, scalar comparison and user-contract constraint checking.
- Added algebraic enums with unit and tuple variants, generic payloads and nested enum values.
- Added nested variant/literal/binding/wildcard patterns with unreachable-arm and exhaustiveness diagnostics.
- Added prelude `Option<T>`, `Result<T,E>`, cross-success-type `?` propagation and standard helpers.
- Added HIR visibility plus direct Linux x86-64 and portable C backend lowering for every new construct.
- Added dual-backend executions, negative diagnostics, examples and documentation regressions.

## PunPun 0.6 development — Step 1

### Compiler foundation

- Added one canonical root `VERSION`, generated runtime/editor mirrors and an executable consistency gate.
- Removed release-version literals from compiler metadata, cache fingerprints, PPX, static-site builders, packaging, publishing and Windows build output names.
- Added parser scaffolding for generic declaration parameters, inline contract constraints and nested constructed types.
- Reserved `enum`, `match`, `case`, `where`, `=>` and postfix `?`; unfinished enum/match/propagation syntax fails explicitly instead of acquiring placeholder behavior.
- Added stable `E0900` diagnostics for reserved 0.6 features.

### Specification and reliability

- Froze the 0.6 generic inference, monomorphization, constraints and overload-selection rules.
- Froze algebraic enum, `Option<T>`, `Result<T,E>`, destructuring, exhaustiveness and non-nullable safe-language rules.
- Froze move-state, borrow-exclusivity and deterministic lexical destruction rules, including the current abort-without-unwinding boundary.
- Added modern/legacy compatibility fixtures, syntax-contract tests, CI version tests and durable anonymous engineering rules.
- Updated the project, documentation, PPX and publishing guides for the 0.6 development cycle.

## PunPun 0.5.0-beta

This beta is a substantial compiler/toolchain rework rather than a cosmetic version bump.

### Compiler and language

- Modern structured PunPun syntax with `bring`, `launch {}`, `fn`, braces and semicolons, while the old `launch: ... done` dialect remains migration-only.
- Native Linux x86-64 backend remains the default; portable C lowering is a compatibility/cross-target backend.
- Objects, structs, constructors, fields, visibility, concrete methods and compile-time `contract ... meets ...` conformance.
- Safe references, mutable references, raw pointers, explicit `unsafe`, `sizeof` and `alignof` foundations.
- Explicit cached C, C++, Rust and assembly `@inject` interoperability with
  compiler discovery, source mapping, and language-specific cache keys.
- Named/default arguments for functions, constructors, and methods.
- Checked ownership transfer through `move`, use-after-move diagnostics, and
  deterministic explicit `drop` for owned objects and numeric buffers.
- Typed HIR/CFG plus local load forwarding, value numbering/CSE, dead-store and
  dead-value elimination.
- Inspectable MIR with SSA-style virtual registers, CFG verification, liveness
  intervals, linear-scan register assignments, and modeled spills.
- PunPun-written bootstrap compiler with its own lexer, recursive-descent parser,
  C17 emitter, stage-one/stage-two fixed-point verification, and consumer CLI.
- Correct SysV zero-extension for native, foreign, and runtime calls returning
  C `bool` values.
- Content-based executable cache, atomic Linux output replacement, stale-build regression coverage, cache explanations, measured timings and build statistics.
- Toolchain selection for detected Clang/GCC-family drivers, linker selection and custom compiler paths.
- Native task-based `async fn`/`await` with concurrent execution, typed task
  results, cooperative cancellation/completion queries, and conservative
  cross-task ownership checks.

### Tooling

- Persistent compiler semantic worker used by the LSP for unsaved-buffer diagnostics.
- VS Code diagnostics, completion, object member completion, semantic tokens,
  hover, definitions, references, token-aware rename, prepare-rename, symbols,
  inlay hints, formatting, signature help, and missing-import quick fixes.
- `pp doctor`, `pp info`, `pp explain`, `pp migrate`, AST/HIR/IR/assembly inspection and toolchain commands.
- PunPunXPac (`ppx`) beta package client, deterministic package integrity checks,
  stable-by-default prerelease resolution, logout/revocation, expiring tokens,
  rate limiting, semantic-version ordering, and a development registry backend.

### Ecosystem

- First-party `requests`, `json`, `gui`, `logging`, `filesystem`, `cli`, and
  `testing` packages with actual compilable implementations. `requests` covers
  GET/POST/PUT/PATCH/DELETE/HEAD, headers, timeout and redirect controls.
- Static documentation site, PPX registry site and PunUI visual designer foundation.
- Linux portable SDK, self-extracting user installer, Arch `PKGBUILD` and package artifact generation.
- WiX v4 Windows MSI/graphical bootstrapper build project. Windows binaries are not produced by the Linux release host unless a Windows cross toolchain and WiX-capable host are available.

### Known beta limitations

- Native task-based `async fn`/`await` and cooperative cancellation are
  implemented across parser, semantics, direct x86-64, portable C, runtime,
  diagnostics and LSP. Event-loop I/O, structured concurrency and coroutine
  state-machine lowering remain beta work.
- Full generics/monomorphization, lifetime/escape analysis, dynamic contract
  dispatch, and a production-hosted PPX registry are not complete. MIR is an
  inspectable analysis representation; native code generation does not yet
  consume it as a full Machine IR pipeline.
- `requests` still lacks streaming, pooling, reusable clients, forms/JSON
  convenience types, and async-native sockets.
- PunUI is an initial native backend/API/design-tool foundation, not a complete production widget toolkit.
- Direct Windows PE/COFF native code generation and Windows installer execution are not validated on this Linux host.
- The self-hosted compiler currently covers its documented bootstrap subset;
  the full production language continues to use the C++ compiler.
- ABI stability is not promised during the 0.x series.
