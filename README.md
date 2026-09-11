<p align="center">
  <img src="assets/punpun-logo.svg" alt="PunPun programming language" width="720">
</p>

<p align="center">
  <strong>One compiler. Three backends. A stable 1.x language.</strong><br>
  PunPun is an ahead-of-time programming language with checked arithmetic,
  ownership analysis, algebraic data types, structured tasks, and compiler-native editor tooling.
</p>

<p align="center">
  <img alt="PunPun 1.3.0" src="https://img.shields.io/badge/version-1.3.0-b9ff4a?style=for-the-badge&labelColor=11151e">
  <img alt="Linux x86-64 validated" src="https://img.shields.io/badge/validated-Linux%20x86--64-66e3ff?style=for-the-badge&labelColor=11151e">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-f6f7fa?style=for-the-badge&labelColor=11151e">
</p>

## PunPun 1.3

PunPun 1.3 makes PPC the repository's canonical compiler. The supplied C++20
compiler is integrated in place, not reimplemented: its lexer, parser, semantic
checker, HIR/MIR pipeline, optimizer, three code generators, VM, and built-in
language server now build and release as the main `ppc` executable.

The language compatibility epoch remains `1.0`; language ABI, runtime ABI,
package format, and lockfile format remain at epoch `1`. The frozen 1.0 builtin
and standard-library surface is checked on every test run.

New in 1.3:

- C, direct x86-64, and register-bytecode backends behind one frontend.
- Built-in LSP over stdio; the editor no longer needs a Node.js language-server bridge.
- Verified HTTPS builtins and `std.net.https`, backed by a dynamically loaded system libcurl.
- Native GUI availability and message-window APIs through `std.gui` (Win32 or X11/XWayland).
- A substantially expanded standard library, compiler regression suite, and generated API reference.
- Compatibility aliases for `go`, `--release`, `--cc-backend`, and `emit-machine-ir`.

## Build from source

Requirements: a C++20 compiler, a C11 compiler, `make`, Python 3 for project and
release tooling, and an assembler/linker for direct-native builds. HTTPS and GUI
support load libcurl and X11 at runtime when available; they are not build-time
dependencies.

```sh
git clone https://github.com/pumpumlang/punpun.git
cd punpun
make compiler
./build/ppc --version
make test
```

Only Linux x86-64 is claimed validated by this source change. Windows and Arch
remain release-qualified only after their real platform workflows pass.

## Quickstart

```sh
./punpun new hello
cd hello
../punpun run
```

Or compile a single file:

```punpun
import std.io

fn greet(name: String) -> String {
    return "Hello, " + name;
}

launch {
    say(greet("PunPun"));
}
```

```sh
./build/ppc check hello.pp
./build/ppc run hello.pp
./build/ppc build -O2 -o hello hello.pp
```

## Compiler pipeline

```text
.pp source -> lexer -> AST -> semantic checker/HIR -> MIR -> optimizer
                                                        |
                              +-------------------------+--------------------+
                              |                         |                    |
                         portable C               native x86-64         bytecode VM
```

Choose a backend explicitly with `--backend=c`, `--backend=native`, or
`--backend=bytecode`. C is the default and has full language coverage. Native
x86-64 intentionally diagnoses unsupported async and oversized parameter lists;
bytecode executes in-process and needs no external toolchain.

Useful inspection commands:

```sh
ppc emit-tokens main.pp
ppc emit-ast main.pp
ppc emit-hir main.pp
ppc emit-mir -O2 main.pp
ppc emit-c main.pp
ppc build --stats --time-passes main.pp
```

## HTTPS

```punpun
import std.net.https

launch {
    let body = https_get("https://example.com");
    if https_ok() {
        say(body);
    } else {
        say(https_error());
    }
}
```

HTTPS requests require certificate and hostname verification, restrict redirects
to HTTPS, cap response bodies at 64 MiB, and expose the latest status/error per
thread. The `requests` package preserves its 1.0 `HttpResponse` API while using
the same runtime service.

## GUI

```punpun
import std.gui

launch {
    if gui_supported() {
        gui_message("PunPun", "Hello from PunPun 1.3");
    }
}
```

Windows uses a Win32 message box. POSIX hosts load X11 dynamically; headless
sessions and Wayland sessions without XWayland report unavailable instead of
failing startup. This is a deliberately small native GUI foundation, not yet a
complete widget toolkit.

## Editor support

PPC owns the language semantics and serves them directly:

```sh
ppc serve --stdio
```

The bundled VS Code extension supports compiler diagnostics, completion, hover,
go to definition, and document symbols. It only registers capabilities the
server currently implements.

## Tooling

```text
pp check [file.pp]              parse and type/ownership check
pp build [file.pp]              compile an executable
pp run [file.pp]                compile and run
pp test [--doc]                 run project tests and optional doctests
pp doc [--check]                generate or verify API documentation
pp fuzz                         deterministic frontend mutation fuzzing
pp compat                       compare supported backend behavior
pp stress                       repeat async/compiler workloads
pp stable-check                 verify the frozen 1.0 public surface
pp doctor                       inspect the installed SDK
pp lsp                          run the built-in language server
```

Projects use `Punpun.toml`. Versioned surfaces derive from the repository
[`VERSION`](VERSION).

`pp add`, `pp remove`, `pp tree`, `pp update` and `pp fetch` are a front end for
[PPX](https://github.com/pumpumlang/punpun-ppx), the package manager, which
installs separately; the toolchain itself does not require it.

## This repository

This repository is the language: the compiler, the runtime, the standard
library, the first-party packages, the specification and the editor
integration. Two companion repositories hold the rest of the project:

| Repository | Contents |
| --- | --- |
| [`punpun-docs`](https://github.com/pumpumlang/punpun-docs) | Documentation site and long-form reference |
| [`punpun-ppx`](https://github.com/pumpumlang/punpun-ppx) | PPX package manager, registry and catalog |

## Documentation

The full documentation is published at
<https://pumpumlang.github.io/punpun-docs/>, with its source in
[`punpun-docs`](https://github.com/pumpumlang/punpun-docs).

In this repository:

- [Generated API reference](docs/api/REFERENCE.md) — produced by `pp doc` from the standard library
- [Error explanations](docs/errors) — the extended text behind `pp explain <CODE>`
- [Compiler internals](compiler/docs/architecture.md) — pipeline, diagnostics and language service
- [Language specification](spec) — the frozen 1.0 epoch
- [Platform support policy](spec/1.0/platform-support.md)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md). Compiler or
runtime fixes need regressions, generated docs must be current, privacy audits
must pass, and platform support is never inferred from another host.

PunPun is distributed under the [MIT License](LICENSE).
