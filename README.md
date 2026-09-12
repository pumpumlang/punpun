<p align="center">
  <img src="assets/punpun-logo.svg" alt="PunPun programming language" width="720">
</p>

<p align="center">
  <strong>One compiler. Three backends. A stable 1.x language.</strong><br>
  PunPun is an ahead-of-time programming language with checked arithmetic,
  ownership analysis, algebraic data types, structured tasks, and compiler-native editor tooling.
</p>

<p align="center">
  <img alt="PunPun 1.5.0" src="https://img.shields.io/badge/version-1.5.0-b9ff4a?style=for-the-badge&labelColor=11151e">
  <img alt="Linux x86-64 validated" src="https://img.shields.io/badge/validated-Linux%20x86--64-66e3ff?style=for-the-badge&labelColor=11151e">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-f6f7fa?style=for-the-badge&labelColor=11151e">
</p>

## Current development

Function literals now form real capturing closures. Free locals are captured by
value into an owned environment; mutable captures retain state across calls,
escaping closures keep that environment alive, nested closures propagate outer
captures, and move-only values transfer ownership into the environment. Function
values remain one-word handles and use the same calling model on the C, direct
x86-64, and bytecode backends. Borrowed `&T`, `&mut T`, and `Slice<T>` values are
conservatively refused as captures until lifetime-aware closure escape analysis
can prove they are safe.

The standard-library expansion is also implemented primarily in PunPun itself:
JSON/TOML, regex, SHA-256/HMAC, compression, ZIP archives, typed configuration,
higher-order collections, UTF-8, paths/filesystem, logging/testing, process and
system helpers, and a small atomic key/value database now exercise the language
rather than hiding equivalent implementations behind injected C.

## PunPun 1.4

PunPun 1.4 closes three gaps that kept ordinary programs from being
expressible: behaviour could not be passed around, sequences could not be
walked, and an interface could not be held as a value.

The language compatibility epoch remains `1.0`; language ABI, runtime ABI,
package format, and lockfile format remain at epoch `1`. Everything below is an
addition, and 0.6 source still builds.

```punpun
contract Shape { fn area() -> int; }

fn total(shapes: List<Shape>) -> int {
    let mut sum = 0;
    for s in shapes { sum = sum + s.area(); }
    return sum;
}

fn apply(g: fn(int) -> int, v: int) -> int { return g(v); }
```

New in 1.4:

- **Functions are values.** `fn(T) -> R` is a type; a named function is a
  value; `fn(x: int) -> int { ... }` can be written inline. The original 1.4
  release did not capture; current development builds add owned closures.
- **Sequences iterate.** `for element in sequence` keeps optimized paths for
  `nums`, `List<T>` and `Slice<T>`, and user-defined types participate through
  the structural `iter()` / `advance() -> Option<T>` iterator protocol.
- **Contracts are types.** `fn draw(s: Shape)` works, and `List<Shape>` holds
  several concrete types at once, dispatching on each value's own identity.
- **One grammar.** The migration dialect warns and names the modern spelling.
  It still parses; `pp migrate` converts a file.
- `sort_by` in the standard library, the comparator that could not previously
  be written.

Carried from 1.3: C, direct x86-64 and register-bytecode backends behind one
frontend; a built-in LSP over stdio; verified HTTPS through `std.net.https`;
the native GUI foundation in `std.gui`.

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
x86-64 supports stack-passed arguments, structured async/task spawning, and a
CFG-aware linear-scan register allocator; bytecode executes in-process and needs
no external toolchain.

Useful inspection commands:

```sh
ppc emit-tokens main.pp
ppc emit-ast main.pp
ppc emit-hir main.pp
ppc emit-mir -O2 main.pp
ppc emit-c main.pp
ppc build --stats --time-passes main.pp
```

## Networking

PunPun has backend-equivalent DNS, TCP and UDP primitives plus structured
HTTP/1.1 and WebSocket modules:

```punpun
import std.net.http

launch {
    let response = http_get("https://example.com");
    say(response.status);
    say(response.header("content-type"));
    say(response.text());
}
```

`std.net.dns`, `std.net.tcp` and `std.net.udp` use runtime-owned socket handles.
The runtime sockets are nonblocking internally; waits, accepts, connects,
sends and receives honor timeouts and task cancellation. `std.net.http` exposes
structured request/response headers and binary bodies, provides a small HTTP/1.1
server surface, and decodes content-length and chunked responses. Plain `http://`
uses those PunPun sockets directly. `https://` uses the reviewed libcurl binding,
with certificate and hostname verification, HTTPS-only redirects, response
headers and binary bodies.

`std.net.websocket` implements RFC 6455 framing over `ws://`, including client
masking, fragmentation, ping/pong and close frames with a 16 MiB message cap.
Raw TLS streams and therefore `wss://` are deliberately not implemented until a
reviewed TLS stream binding exists; PunPun does not invent its own cryptography.
The high-level HTTP client currently opens one connection per request rather than
maintaining a keep-alive pool.

## GUI

```punpun
import std.gui

launch {
    let window = GuiWindow("Counter", 420, 220);
    let label = window.label("Ready");
    let button = window.button("Click me");
    gui_vbox(window.widgets, 20, 20, 380, 36, 10);

    let wh = window.handle;
    let bh = button.handle;
    let mut count = 0;
    let handler = fn(event: GuiEvent) -> void {
        if event.is_click() && event.widget == bh {
            count = count + 1;
            gui_widget_set_text(label.handle, "Clicks: " + text(count));
        }
        if event.is_close() { gui_window_close(wh); }
    };
    gui_run(window, handler);
}
```

`std.gui` is now a retained cross-platform toolkit rather than only a message
window. It provides windows, labels, buttons, text inputs, checkboxes, sliders,
progress bars, panels and canvases; widget text/value/visibility/enabled/bounds
state; vertical, horizontal and grid layouts; keyboard/mouse/change/resize/paint
events; application-posted events; closure-driven event loops; modal alerts and
confirm helpers; and RGB canvas drawing.

Windows uses Win32 controls and GDI. POSIX hosts dynamically load X11 and work
through XWayland where available, so X11 remains a runtime rather than build
dependency. `PUNPUN_GUI_HEADLESS=1` activates the same retained state model with
no native display for deterministic CI/application tests. Canvas drawing is
immediate-mode: redraw it when a `paint` event arrives. Native Wayland and full
Unicode input-method integration remain separate platform work rather than being
faked by the portable layer.

## Standard library

Current development builds deliberately move ordinary application logic into
PunPun itself rather than growing the runtime into a second hidden language.
The runtime supplies host operations such as files, clocks, secure random bytes
and process execution; parsing, formatting, algorithms, containers and policy
live in `.pp` modules and therefore exercise the same compiler users do.

Notable modules include:

| Module | Surface |
| --- | --- |
| `std.data.json` | recursive JSON value model, parser, serializer, pretty-printer and typed accessors |
| `std.data.toml` / `std.config` | TOML parsing plus layered typed application configuration |
| `std.regex` | PunPun backtracking regex engine with classes, anchors, quantifiers, search/replace/split |
| `std.crypto.sha256` | SHA-256, HMAC-SHA256 and constant-time digest comparison |
| `std.compress.lzss` / `std.compress.rle` | pure-PunPun byte compression codecs |
| `std.archive.zip` | interoperable ZIP read/write for stored UTF-8 entries with CRC-32 validation |
| `std.db.kv` | atomic JSON-backed document/key-value store |
| `std.collections.functional` | generic `map`, `filter`, `fold`, `zip`, `enumerate`, predicates and callbacks |
| `std.collections.deque` | generic amortized deque |
| `std.datetime` / `std.path` / `std.filesystem` | date arithmetic/ISO formatting and higher-level file/path operations |
| `std.process` / `std.system_info` | captured processes, shell-safe argument assembly and host/environment information |
| `std.logging` / `std.testing` | structured log levels/file sinks and reusable test-suite/benchmark helpers |
| `std.random` / `std.text.utf8` / `std.data.mime` | deterministic RNG, secure tokens, UTF-8 codepoints and MIME lookup |

The ZIP module currently implements standard method-0 (stored) entries rather
than DEFLATE/ZIP64. `std.compress` provides PunPun-native compression for
application data, while interoperable DEFLATE remains future work. The KV store
is intentionally a small atomic document database, not a disguised claim that a
single JSON file is SQLite. These limits are explicit so applications can choose
appropriate dependencies rather than discover them through disappointment.

A runnable cross-section lives in [`examples/stdlib-toolbox.pp`](examples/stdlib-toolbox.pp).

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
