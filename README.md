<p align="center">
  <img src="assets/punpun-logo.svg" alt="PunPun" width="720">
</p>

# PunPun

**PunPun 0.5.0-beta** is an ahead-of-time native programming language built
around fast edit/check/run cycles, concrete object-oriented programming,
value-oriented systems programming, and explicit low-level escape hatches.

The normal Linux x86-64 path is direct:

```text
.pp source
  → PunPun lexer/parser
  → semantic analysis
  → typed HIR / CFG
  → native x86-64 backend
  → assembler + linker
  → native ELF executable
```

There is no interpreter or VM in the ordinary build/run path. A portable C
backend exists for portability and Windows cross-building; explicit
`@inject->c` is a separate opt-in interoperability feature, not how ordinary
PunPun source is compiled.

> **Status:** 0.5.0-beta is active compiler development. Native compilation,
> modern syntax, objects/value types, safe-reference/raw-pointer foundations,
> explicit move/drop checking, contracts, local packages, compiler-backed LSP
> diagnostics, MIR/liveness inspection, a fixed-point self-hosted compiler, and
> C/C++/assembly injection work today.
> The remaining production boundaries are tracked precisely in
> [`COMPLETION_REPORT.md`](COMPLETION_REPORT.md).

## A recognizable PunPun program

```punpun
bring std::io;

launch {
    let language = "PunPun";
    say("Hello from PunPun!");
}
```

PunPun keeps `bring`, `launch`, and `say` as concise language personality while
using structured braces, semicolons, static semantics, and native compilation.
The older `launch: ... done` dialect is migration-only during the beta.

## Objects without Java ceremony

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

`object` is identity/reference-oriented. `struct` remains the value-oriented,
inline/stack-friendly choice. Concrete method calls are statically dispatched;
contracts currently enforce compile-time conformance without forcing a vtable.

## Low-level when you need it

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

Safe references and explicit mutability are available in ordinary code. Raw
pointer creation/dereference and pointer arithmetic are gated behind `unsafe`.
The compiler diagnoses escaping local references and invalid mutable borrows in
its current safety foundation.

## Explicit native interoperability: `@inject`

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

Injected C is content-addressed and compiled to an object only during
`build`/`run`. Unchanged injection objects are reused from
`.punpun/cache/inject/`. `pp check` and editor operations never execute or
compile foreign source. Projects without `@inject` do not initialize or spawn
the foreign compiler path.

C, C++, Rust, and assembly adapters share this content-addressed pipeline. The
compiler discovers the corresponding host compiler and fails with a capability
diagnostic when it is unavailable. Rust execution is covered conditionally on
hosts with `rustc`; C, C++, and assembly are exercised by the local suite.

## Fast development loop

```sh
pp check
pp run
pp build --release
```

Measurements from the bundled Linux build environment for the tiny Hello World
above:

| Operation | Median |
| --- | ---: |
| `ppc check main.pp` | **2.244 ms** |
| cold native build | **27.176 ms** |
| no-change native build | **2.582 ms** |
| executable startup + exit | **0.935 ms** |
| unsaved edit → LSP diagnostic | **21.440 ms** |

Environment: Linux 6.18.35 x86-64, Intel Xeon Platinum 8573C environment,
GCC 14.2.0 / GNU ld 2.44. These are engineering measurements from one machine,
not claims that PunPun universally beats C/C++/Rust. Full procedure and ranges
are in [docs/performance/PERFORMANCE.md](docs/performance/PERFORMANCE.md).

## Install on Linux x86-64

The release includes a self-extracting user-local installer:

```sh
bash Punpun-0.5.0-beta-Linux-x86_64-Installer.run
```

It installs the toolchain under:

```text
~/.local/share/punpun
~/.local/bin/pp
~/.local/bin/ppc
~/.local/bin/punpun
```

and installs the bundled VS Code/Code OSS/VSCodium extension when a supported
editor CLI is available. No root installation is required.

Then:

```sh
pp --version
pp doctor
pp new hello
cd hello
pp run
```

Uninstall with `punpun-uninstall`.

## CLI

```text
pp new <name>                 scaffold a package
pp init [name]                initialize the current directory
pp add <name> <path>          add a local/path dependency
pp remove <name>
pp update | fetch | tree

pp check [file.pp]            frontend + semantic diagnostics only
pp build [file.pp]
pp build [file.pp] --release
pp build [file.pp] --timings
pp build [file.pp] --cache-info
pp run [file.pp] [-- args...]
pp test
pp fmt [file.pp]
pp clean

pp doctor                     inspect the local toolchain/editor dependencies
pp info                       show compiler/target/cache locations
pp explain E0201              extended compiler-error documentation
pp migrate [file.pp]          convert common 0.4 syntax + reformat/check

pp ast [file.pp]
pp ir [file.pp]
pp asm [file.pp]
pp emit-tokens [file.pp]
pp emit-ast [file.pp]
pp emit-hir [file.pp]
pp emit-ir [file.pp]
pp emit-c [file.pp]
pp emit-asm [file.pp]

pp toolchain probe [gcc|clang|zig|mingw]
pp selfhost input.pp output.c

pp lsp
pp editor install-vscode
```

Single-file scripts do not require a manifest. `Punpun.toml` projects support
local/path package graphs and deterministic `Punpun.lock` metadata.

## VS Code is connected to the compiler now

The extension no longer has a separate regex "type checker." It launches the
PunPun LSP, which keeps one `ppc semantic-worker` process alive and sends
**unsaved in-memory buffers** through the same parser and semantic analyzer used
by `pp check`.

This means the following is a permanent regression test:

```punpun
bring std::stats;

launch {
    say("hello from testproj");
    sdds;
}
```

`pp check` and VS Code both report `E0201` on exactly `sdds`. The extension also
provides semantic tokens, object-member completion, constructor/method signature
help, hover, definition, references, rename, symbols, formatting, inlay hints,
and quick fixes for supported compiler suggestions.

Manual extension install:

```sh
code --install-extension dist/punpun-vscode-0.5.0-beta.vsix --force
```

## Repository layout

```text
compiler/          lexer, parser, semantics, HIR, optimizers, native backends
selfhost/          PunPun-written compiler, bootstrap, fixed-point test, examples
runtime/           small native runtime and target-facing C ABI
stdlib/            PunPun standard-library modules
tooling/lsp/       compiler-backed language server
tooling/migrate.py 0.4 → 0.5 migration helper
editors/vscode/    VS Code client, grammar, snippets, bundled LSP
assets/            PunPun SVG brand masters + raster icons
tests/             compiler/runtime/LSP/package/injection regression tests
docs/              language, objects, injection, diagnostics, performance
```

## Build the compiler

Requirements for a source build are a C17 compiler, C++17 compiler, Make, `ar`,
and a normal linker toolchain.

```sh
make clean
make
python -m unittest discover -s tests
```

The release also includes a prebuilt Linux x86-64 `build/ppc` bootstrap binary.

## Self-hosting compiler

PunPun now includes `selfhost/ppc_self.pp`, a compiler implemented in PunPun
itself. Its bootstrap is verified to a fixed point: stage zero builds the
PunPun source, stage one recompiles that same source to C17, stage two repeats
the translation, and the generated stage-one/stage-two C must be byte-identical.

```sh
make selfhost
pp selfhost selfhost/examples/hello.pp build/selfhost/hello.c
```

The bootstrap compiler has its own lexer, recursive-descent parser, lowering,
and deterministic C emitter. It currently accepts a documented closed subset,
so it is a real self-hosting seed rather than a complete replacement for the
production C++ compiler. See [selfhost/README.md](selfhost/README.md).

## Current implementation boundaries

Implemented and tested foundations include:

- brace-based PunPun 0.5 syntax with `bring`, `launch`, and `say`;
- source spans and structured diagnostics;
- objects, constructors, visibility, fields, implicit/explicit receiver methods;
- value-type structs;
- compile-time `contract` / `meets` conformance;
- `&T`, `&mut T`, raw `*T`, `&raw`, `unsafe`, `sizeof`, `alignof`;
- direct Linux x86-64 code generation and native executable linking;
- portable C backend and MinGW-oriented Windows cross-build path;
- typed HIR/CFG plus constant folding, local value numbering/CSE, load
  forwarding, dead-store/dead-value elimination, and dead-block cleanup;
- inspectable MIR with virtual SSA values, CFG verification, liveness intervals,
  linear-scan register assignments, and spill-slot modeling;
- executable/injection caches and local/path package graph;
- persistent semantic-worker LSP and VS Code integration;
- cached C/C++/Rust/assembly `@inject` adapters with source-mapped diagnostics;
- named/default arguments for functions, constructors, and methods;
- cooperative async task cancellation and completion inspection;
- UTF-8 validation/counting and filesystem/path runtime helpers.
- a PunPun-written compiler with verified stage-one/stage-two fixed-point output.

Still incomplete for production:

- full ownership/lifetime/escape model beyond the checked move/drop vertical slice;
- generic types/monomorphization and dynamic contract/interface values;
- migration of native code generation from the inspectable MIR foundation to a
  complete Machine IR/instruction-selection/register-allocation pipeline;
- expansion of the self-hosting subset to the complete production language and
  replacement of the C++ stage-zero implementation;
- complete ABI classification for aggregates/floats in direct foreign calls;
- C-compatible layout attributes and broad FFI surface;
- production-hosted package registry and complete dependency solving;
- direct Windows PE/COFF backend;
- full debug info/stack traces, atomics/SIMD and production event-loop async I/O.

Native `async fn`/`await`, cooperative cancellation, and task completion queries
are implemented and tested through both direct x86-64 and portable C backends.
The remaining async work is an event-driven I/O reactor, structured task groups,
and compiler-generated coroutine/state-machine lowering.

The beta intentionally labels those as incomplete rather than shipping empty
classes with triumphant names.

## Documentation

- [Grammar overview](docs/language/grammar.md)
- [Objects and contracts](docs/objects/README.md)
- [`@inject` interoperability](docs/injection/README.md)
- [Async tasks](docs/language/async.md)
- [Performance measurements](docs/performance/PERFORMANCE.md)
- [`pp explain` error documentation](docs/errors/)

## Contributing

Build the compiler, run the entire test suite, and add a regression test for
compiler bugs. Native-code changes should be validated through both direct x86
and portable C paths where applicable. Performance changes should be measured
before claims are added to documentation.

## License

PunPun is distributed under the [MIT License](LICENSE).

## PunPunXPac (PPX) and first-party packages

PunPun 0.5 ships the beta `ppx` client and a local development registry implementation. It uses the same `Punpun.toml` package model as the compiler rather than inventing a second dependency universe.

```sh
ppx --version
ppx search requests
ppx add requests
pp tree
```

Bundled first-party packages currently include real compilable foundations for `requests`, `json`, `gui`, `logging`, `filesystem`, `cli`, and `testing`. `requests` uses system libcurl dynamically, so ordinary PunPun programs do not pay an HTTP/TLS startup or link cost. The registry backend lives in `ppx-registry/`; the browser frontend lives in `ppx-site/`.

## Documentation and GUI designer

`docs-site/` is a dependency-free static documentation site project with responsive dark/light UI, navigation, search and copyable code examples. `gui-maker/` contains the PunUI visual designer beta foundation: a widget palette, live browser preview, property editing and PunPun source generation.

## Build correctness

The build cache is content-based. A permanent regression test edits a source file from `VERSION A` to `VERSION B` while deliberately preserving the exact filesystem timestamp; the next build must still miss the cache and execute `VERSION B`. Final executables are written to a sibling staging path and atomically renamed into place on Linux only after compilation/linking succeeds.

Inspect cache behavior with:

```sh
pp build --cache-info
pp build --stats
```

## Toolchains

```sh
pp toolchain detect
pp toolchain list
pp toolchain info clang
pp toolchain probe gcc clang
pp build --toolchain clang
pp build --toolchain gcc
pp build --linker lld
```

Clang and GCC-family toolchains are exercised when present. Windows/MSVC and Zig adapters are capability-gated and are not claimed as tested on hosts where those tools are absent.

## Beta boundaries

PunPun 0.5.0-beta is usable for native experimentation, but it is not pretending
to be 1.0. Native task-based `async fn`/`await` and cooperative cancellation are
working; coroutine state-machine lowering and event-loop I/O are not complete.
Production generics, complete lifetime analysis, dynamic contract dispatch, a
production-hosted PPX registry, and direct Windows PE/COFF emission also remain
unfinished. See `COMPLETION_REPORT.md` and `PROJECT_STATUS.txt` for the exact
engineering boundary.
