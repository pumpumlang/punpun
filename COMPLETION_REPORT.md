# PunPun continuation engineering report

Date: 2026-09-08  
Version: 0.5.0-beta continuation

This report is the release gate for the continuation archive. It distinguishes
implemented and tested code from platform work that cannot honestly be certified
on the Linux build host. No placeholder MSI, setup EXE, Arch package-manager
result, signature, or native binary is represented as a real artifact.

## Implemented in this continuation

- Added `selfhost/ppc_self.pp`, a compiler written entirely in PunPun with its
  own lexer, recursive-descent parser, C type/call lowering, deterministic C17
  emitter, CLI integration, and documented bootstrap subset.
- Added a fixed-point bootstrap: the PunPun-built stage-one compiler compiles
  itself, the resulting stage-two binary compiles itself again, and both C
  outputs are required to be byte-identical before release.
- Fixed SysV ABI normalization for `bool` values returned by PunPun functions,
  native calls, and C runtime builtins.
- Added checked `move` and explicit deterministic `drop` for owned objects and
  numeric buffers, including use-after-move diagnostics and reinitialization.
- Added named and literal-default arguments for free functions, constructors,
  and methods, with diagnostics for unknown, duplicate, and missing arguments.
- Added an inspectable MIR with SSA-style virtual registers, CFG verification,
  liveness intervals, linear-scan physical-register assignments, and spill-slot
  modeling. `pp emit-ir` exposes it while `pp emit-hir` retains HIR output.
- Added local load forwarding, value numbering/CSE, dead-store elimination, and
  dead-value elimination to release optimization.
- Added cooperative task cancellation and completion queries across semantics,
  both backends, and the native runtime.
- Extended `requests` with GET, POST, PUT, PATCH, DELETE, HEAD, headers, timeout,
  redirects, thread-local status/error state, and owned response cleanup.
- Added UTF-8 validation/code-point counting plus directory, rename, remove, and
  path-join runtime/stdlib APIs.
- Extended injection to C++, Rust, and assembly with compiler discovery,
  language-specific caching, source mapping, and conditional capability tests.
- Hardened PPX with stable-by-default prerelease resolution, semantic-version
  ordering, token expiry/revocation, logout, rate limiting, and security headers.
- Improved LSP prepare-rename, token-aware references/rename, and missing-stdlib-
  import quick fixes.
- Added `.file`/`.loc` native mappings and C `#line` mappings for source-level
  debugger correlation.
- Added capability probes for GCC, Clang, Zig, and MinGW families.
- Added deterministic 10/100/500/1000-module benchmark generation with cold,
  warm, and one-function-edit measurements.
- Added Linux, Arch, and Windows release CI definitions. The Windows job builds
  real binaries and WiX artifacts on Windows; the Arch job performs actual
  `makepkg`, install, upgrade, smoke, and removal operations.

## Verified on this host

- Clean C17/C++17 warnings-as-errors build.
- Full compiler/runtime/tooling test suite.
- Direct x86-64 and portable C execution for the new language/runtime features.
- C, C++, and assembly injection execution; Rust injection remains conditional
  because `rustc` is unavailable on this host.
- GCC toolchain capability probe: C17, C++20, assembly, linking, and execution.
- Native source mappings inspected in generated assembly and ELF debug lines.
- Benchmark harness smoke-tested with deterministic generated module graphs.
- Self-hosted stage-one/stage-two fixed point and independently compiled example.

## Still not production-complete

The following roadmap claims would require substantially larger designs or a
different native host and are deliberately not mislabeled as finished:

- Real Windows MSI/setup output, installation/upgrade/uninstallation, PATH/file
  association checks, Windows native compilation, and real Arch/CachyOS package
  installation/removal. CI now defines these gates but has not run them here.
- Production generics/monomorphization, generic constraints, `Option`/`Result`,
  algebraic enums, exhaustive matching/destructuring, and final nullable values.
- Contract-typed dynamic dispatch/vtables, properties/static members, automatic
  destruction, devirtualization, and a final inheritance model.
- Full move/lifetime/escape analysis, lexical `Drop`, slices, allocators/arenas,
  C-layout attributes, atomics/volatile operations, and complete ABI validation.
- Native code generation driven end-to-end by MIR/Machine IR. The new MIR and
  register allocation are inspectable foundations; the direct backend still
  lowers from the existing frontend representation.
- Full-language self-hosting. The current compiler is a genuine fixed-point
  bootstrap seed, but objects, imports, async, inference, and other production
  language features still require the C++ compiler.
- Global optimizer work such as SCCP, alias/escape analysis, scalar replacement,
  loop/vector optimization, LTO, and PGO.
- An event-driven async reactor, structured concurrency/task groups, async-native
  sockets/files, scalable timers/wakers, streaming/pooling clients, and complete
  `Result` integration.
- A complete stdlib, production PunUI widget/layout/accessibility/native backend,
  public hosted PPX service with organizations/signing/admin recovery, and full
  semantic IDE/debugger integration.
- ARM64/macOS targets, direct PE/COFF emission, Windows PDB information, and
  function/interface-granular incremental compilation.

These boundaries are intentional release truthfulness, not empty stubs. The
archive contains the implementation, regression tests, platform CI gates, and
benchmarking needed to continue each area without inventing successful results.
