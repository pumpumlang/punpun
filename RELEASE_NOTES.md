# PunPun 0.6.0-beta

PunPun 0.6 completes the language-foundation cycle: generics and algebraic data types now sit beside a real ownership/borrow pass, a mandatory verified HIR/MIR pipeline, and three selectable native code-generation paths on supported hosts.

## Highlights

- Learning documentation now follows a beginner → intermediate progression with consistent Overview / Syntax / Runnable example / Common mistakes / Next steps sections and compiler-checked examples.
- `.pp` source icons are integrated with VS Code language contributions, Linux shared MIME/hicolor conventions, and the Windows MSI file-type ProgID.
- PPX now supports a complete author-to-consumer flow: register/login, dry-run validation, publish/upload, immutable metadata, checksum-verified download, and install.
- Publisher synchronization now removes stale repository/site files by replacement and prunes outdated uploaded assets from the current release tag only.
- Generic functions, structs, objects and methods with deterministic monomorphization and checked constraints.
- Algebraic enums, nested destructuring, exhaustive/reachability diagnostics, `Option<T>`, `Result<T,E>` and postfix `?`.
- Control-flow move-state analysis (`initialized`, `moved`, `maybe moved`) with explicit `move`/`drop`, reinitialization and use-after-move diagnostics.
- Safer shared/mutable borrow checks and non-owning checked `Slice<int>` views (`view`, `slice_len`, `slice_get`).
- Lexical destruction for supported move-only identity objects on normal scope exits.
- Mandatory typed HIR → verified MIR construction for `check`, builds and backend emission, with deterministic function/body fingerprints used by incremental build identity.
- Direct Linux x86-64 backend remains the default native path.
- Portable C backend remains available through `--cc-backend`.
- New optional Clang/LLVM path through `--llvm-backend`, plus `emit-llvm` for inspectable LLVM IR. It uses the same PunPun frontend/semantics/ownership/HIR/MIR pipeline rather than becoming a separate language implementation.
- Existing 0.5 behavior remains regression-tested, including the legacy shared-handle semantics of `nums`.
- Self-host fixed-point bootstrap, PPX, compiler-backed LSP/VS Code tooling, static sites and release validation remain part of the release gate.

## LLVM usage

```sh
ppc run main.pp --llvm-backend
ppc build main.pp --release --llvm-backend
ppc emit-llvm main.pp -o main.ll
```

Set `PUNPUN_LLVM_CC` to select a Clang-compatible executable. The 0.6 LLVM compatibility backend is native-host only and is optional.

## Compatibility and memory-model notes

`nums` remains a 0.5-compatible shared list handle in 0.6, so ordinary assignment and function parameter passing alias it. Explicit `move(nums)` still ends the source binding for move-state analysis. New future owned collections may use different ownership semantics without retroactively breaking `nums`.

Panics abort and do not unwind the stack in 0.6. Lexical destruction is therefore guaranteed on normal control flow, not during aborting panic unwinding. Custom user-defined destructor hooks are later work.

## Platform qualification

The Linux x86-64 artifacts are built and executed on the release host. Windows WiX projects and Arch package payloads are generated and structurally validated here, while actual Windows install/upgrade/uninstall and real `pacman` qualification remain CI/real-host gates.
