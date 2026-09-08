<p align="center">
  <img src="assets/punpun-logo.svg" alt="PunPun programming language" width="700">
</p>

<p align="center">
  <strong>A fast, native programming language with personality.</strong><br>
  Write concise <code>.pp</code>. Build real machine-code executables. Keep the low-level controls explicit.
</p>

<p align="center">
  <img alt="Version 0.6 development" src="https://img.shields.io/badge/version-0.6%20development-b9ff4a?style=for-the-badge&labelColor=11151e">
  <img alt="Linux x86-64" src="https://img.shields.io/badge/Linux-x86--64-66e3ff?style=for-the-badge&labelColor=11151e">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-f6f7fa?style=for-the-badge&labelColor=11151e">
</p>

<p align="center">
  <a href="#quick-start">Quick start</a> ·
  <a href="#language-tour">Language tour</a> ·
  <a href="#toolchain">Toolchain</a> ·
  <a href="#punpunxpac-ppx">PPX</a> ·
  <a href="COMPLETION_REPORT.md">Project status</a>
</p>

---

PunPun 0.6 development is an ahead-of-time native language focused on fast edit/check/run cycles, concrete object-oriented programming, value-oriented systems work and clear escape hatches for native interoperability.

```text
.pp source → parser + semantics → typed HIR/MIR → x86-64 or C backend → native executable
```

Ordinary builds use no interpreter or virtual machine. Linux x86-64 has a direct backend; the portable C backend supports additional toolchains and Windows-oriented builds. Foreign source runs only through explicit `@inject` blocks.

> **Development releases keep honest boundaries.** The compiler, package client, editor tooling, native builds and self-hosting seed are usable and tested. The frozen 0.6 generic, enum and ownership rules are specifications—not claims that every feature is already executable. See [ROADMAP.md](ROADMAP.md) and [COMPLETION_REPORT.md](COMPLETION_REPORT.md).

## 0.6 Step 1 foundation

The first 0.6 milestone removes version drift and turns major language choices into testable contracts:

- one canonical [`VERSION`](VERSION) drives the compiler, runtime, PPX, editor, sites, packages and installers;
- [`spec/0.6/`](spec/0.6/) freezes generics, constraints, monomorphization, enums, `Option`, `Result`, matching, nullability, moves, borrows and deterministic destruction;
- the frontend parses generic declaration headers and nested generic type spellings for tooling;
- future `enum`, `match` and propagation syntax is reserved with `E0900` until its implementation milestone;
- compatibility fixtures keep valid 0.5 modern and migration syntax working;
- CI paths derive their artifact names from `VERSION` instead of an old release string.

## Quick start

Install the Linux x86-64 release without root:

```sh
bash PunPun-*-Linux-x86_64-Installer.run
```

Open a new terminal, then create and run a project:

```sh
pp doctor
pp new hello
cd hello
pp run
```

The installer places the SDK in `~/.local/share/punpun` and commands in `~/.local/bin`. Remove it later with `punpun-uninstall`.

## Language tour

### Hello, native world

```punpun
bring std::io;

launch {
    let language = "PunPun";
    say("Hello from " + language + "!");
}
```

### Objects and contracts

```punpun
contract Damageable {
    fn damage(amount: i64);
}

object Enemy meets Damageable {
    private let mut health: i64;

    public init(health: i64) {
        self.health = health;
    }

    public fn damage(amount: i64) {
        self.health -= amount;
    }

    public fn hp() -> i64 {
        return self.health;
    }
}

launch {
    let mut enemy = Enemy(100);
    enemy.damage(25);
    say(enemy.hp());
}
```

`object` is identity-oriented; `struct` is the inline value-oriented choice. Concrete methods are statically dispatched, while contracts currently provide compile-time conformance.

### References, pointers and `unsafe`

```punpun
fn bump(value: &mut i64) {
    *value = *value + 1;
}

launch {
    let mut value = 41;
    bump(&mut value);

    unsafe {
        let pointer: *i64 = &raw value;
        *pointer = *pointer + 1;
    }

    say(value);
}
```

Safe references live in ordinary code. Raw-pointer creation, dereference and arithmetic require an explicit `unsafe` region.

### Native interoperability

```punpun
@inject->c("""
#include <stdint.h>
int64_t fast_native_add(int64_t a, int64_t b) {
    return a + b;
}
""");

extern native fn fast_native_add(a: i64, b: i64) -> i64;

launch {
    say(fast_native_add(10, 20));
}
```

C, C++, Rust and assembly injections are content-addressed and cached. `pp check` and editor analysis never compile or execute injected source.

## What ships today

| Area | Available now |
| --- | --- |
| Compilation | Direct Linux x86-64 backend, portable C backend, assembler/linker integration |
| Language | Functions, objects, structs, contracts, references, pointers, async tasks, named/default arguments |
| Correctness | Structured diagnostics, content-hash builds, atomic executable replacement, regression suite |
| Developer tools | `pp` project CLI, compiler-backed LSP, VS Code extension, formatter and migration helper |
| Ecosystem | PPX client, local/path graphs, lockfiles, seven bundled first-party packages |
| Interop | Cached C/C++/Rust/assembly injection and native ABI foundation |
| Self-hosting | PunPun-written bootstrap compiler with verified fixed-point output |

## Toolchain

The everyday loop is intentionally small:

```sh
pp check
pp run
pp build --release
```

Useful commands:

```text
pp new <name>                 create a package
pp init [name]                initialize this directory
pp add <name> <path>          add a local dependency
pp remove <name>
pp update | fetch | tree

pp check [file.pp]            parse and type-check
pp build [file.pp]            build a native executable
pp run [file.pp]              build and run
pp test                       run tests/*.pp
pp fmt [file.pp]              format source
pp clean                      clear this project's cache

pp doctor                     inspect SDK dependencies
pp explain E0201              explain a diagnostic
pp migrate [file.pp]          migrate common 0.4 syntax
pp ast | ir | asm             inspect compiler stages
pp emit-c | emit-asm          emit backend source
pp toolchain detect           probe installed toolchains
pp editor install-vscode      install editor support
```

Single files work without a manifest. Projects use `Punpun.toml` and a deterministic `Punpun.lock`.

## PunPunXPac (PPX)

PPX shares the compiler's manifest and lockfile model:

```sh
ppx search requests
ppx add requests
ppx tree
ppx doctor
```

The SDK includes `requests`, `json`, `gui`, `logging`, `filesystem`, `cli` and `testing`. The public PPX website contains a built-in searchable catalog, so it works even before the production registry backend is hosted. See [ppx/README.md](ppx/README.md) and [ppx-site/README.md](ppx-site/README.md).

## Editor experience

The VS Code extension launches the real PunPun language server. Unsaved buffers go through the same parser and semantic analyzer used by `pp check`, providing diagnostics, semantic tokens, completion, signature help, hover, definition, references, rename, symbols, formatting, inlay hints and supported quick fixes.

```sh
pp editor install-vscode
```

## Build from source

Requirements: a C17 compiler, C++17 compiler, Make, `ar`, Python 3, Node.js and a normal linker toolchain.

```sh
make clean all
./tests/run.sh
make selfhost
```

Build the complete release bundle with:

```sh
python3 scripts/release.py
```

Version and policy checks can also be run directly:

```sh
python3 scripts/sync_version.py
make version-check
python3 scripts/privacy_audit.py .
```

## Self-hosting

`selfhost/ppc_self.pp` contains a compiler written in PunPun. The bootstrap builds it, recompiles the same source and requires stage-one and stage-two generated C to be byte-identical.

```sh
make selfhost
pp selfhost selfhost/examples/hello.pp build/selfhost/hello.c
```

It is a genuine deterministic compiler seed for a documented subset, not yet a complete replacement for the production C++ compiler. Details live in [selfhost/README.md](selfhost/README.md).

## Repository map

```text
compiler/          frontend, semantics, HIR/MIR, optimizers and backends
runtime/           native runtime and target-facing C ABI
stdlib/            core PunPun modules
packages/          first-party PPX packages
ppx/               package-manager client
ppx-registry/      development registry API
ppx-site/          deployable package-catalog website
docs-site/         deployable documentation website
tooling/lsp/       compiler-backed language server
editors/vscode/    VS Code extension
selfhost/          compiler written in .pp
tests/             compiler, runtime, tooling and packaging regressions
scripts/           release, validation, benchmark and publishing tools
spec/              normative language and compatibility decisions
```

## Performance

The repository contains reproducible compiler, incremental-build, startup and editor-latency measurements—not universal claims against other languages. Read [docs/performance/PERFORMANCE.md](docs/performance/PERFORMANCE.md) for the environment, commands and complete results.

## Documentation and publishing

- Language documentation: [docs/](docs/)
- Deployable docs project: [docs-site/](docs-site/)
- Release status: [PROJECT_STATUS.txt](PROJECT_STATUS.txt)
- Exact completion boundary: [COMPLETION_REPORT.md](COMPLETION_REPORT.md)
- One-command publication: [publish-punpun.sh](publish-punpun.sh)

## Contributing

Build the compiler, run the entire suite and add a regression test for every compiler bug. Validate native-code changes through direct x86-64 and portable C paths where applicable. Measure performance changes before documenting them.

## License

PunPun is distributed under the [MIT License](LICENSE).
