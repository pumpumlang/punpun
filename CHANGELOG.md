## 0.6.0-beta — 2026-09-08

- Completed Steps 4–6: move-state/borrow analysis, checked slices, lexical object destruction, mandatory verified HIR/MIR, deterministic compiler fingerprints, release qualification and compatibility gates.
- Added optional `--llvm-backend` and `emit-llvm` using Clang/LLVM after the shared PunPun frontend/MIR pipeline.
- Preserved 0.5 `nums` shared-handle behavior while keeping explicit `move(nums)` available for binding-lifetime transfer.
- Fixed Step 3 portable-backend match warnings, method-receiver ownership, minimum-int HIR typing, and regressions exposed by mandatory MIR verification.

# Changelog

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
