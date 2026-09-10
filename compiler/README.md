# ppc — the PunPun compiler

The canonical compiler for [PunPun](https://github.com/pumpumlang/punpun) 1.3,
implemented in C++20 with a C11 runtime. PunPun 1.3 integrates this supplied
compiler in place as the repository's `ppc`; it does not maintain a second
reference compiler.

PPC preserves the stable PunPun 1.0 language and ABI epochs while adding its
three-backend pipeline, native language service, expanded standard library, and
runtime-backed HTTPS and GUI libraries.

## Quick start

```sh
make compiler              # from the repository root; builds build/ppc
make compiler-test         # all three backends plus the LSP protocol suite

./build/ppc run hello.pp
./build/ppc build -O2 -o game main.pp
./build/ppc check main.pp
./build/ppc explain E0800
```

## Three backends, one front end

The same lexer, parser, checker, and optimizer feed three code generators. They
differ only in what they emit, which puts them at different points on the
compile-time / run-time curve. Measured on `examples/algorithms/sieve.pp`
(sieve of Eratosthenes to 200,000, five rounds):

| `--backend=` | compile | run | needs a toolchain? |
|---|---|---|---|
| `bytecode` | 3 ms | 102 ms | no |
| `native` | 25 ms | 24 ms | assembler and linker |
| `c` (default) | 58 ms | 14 ms | C compiler |

- **`bytecode`** compiles to a register-based VM that runs in-process. Nothing
  is written to disk and no external tool runs, which makes it the fastest path
  from source to output — good for iteration, tests, and scripting.
- **`native`** emits x86-64 System V assembly directly. Every value lives in a
  stack slot rather than a machine register, so the code is roughly what a
  non-optimizing compiler produces, but it skips C entirely.
- **`c`** emits one C translation unit and hands it to the host compiler, which
  then inlines and register-allocates across the whole program. Slowest to
  compile, fastest to run, and portable anywhere there is a C compiler.

All three produce identical program output. The test suite runs every case
against every backend for exactly that reason.

### Coverage

`c` supports the whole language. The other two refuse what they cannot compile,
with a diagnostic naming the gap — they never silently produce different
behavior:

| | `c` | `native` | `bytecode` |
|---|---|---|---|
| full language | yes | yes | yes |
| `async` / `await` | yes | no (E1000) | runs tasks inline |
| `@inject` foreign source | yes | no | no |
| `extern native fn` | yes | yes | no |
| more than 6 int / 8 float parameters | yes | no (E1000) | yes |

## Optimization

```
-O0        no transforms; fastest possible compile
-O1        one cheap local sweep                    [default]
-O2        full pipeline, iterated to a fixed point
--unchecked  use raw machine arithmetic
```

Integer arithmetic is checked by default, as the language specifies: overflow,
division by zero, and out-of-range shifts all trap with a message rather than
wrapping. The optimizer knows this — it will **not** constant-fold an expression
that would overflow, because doing so would turn a runtime trap into a silently
wrong answer. `--unchecked` opts out and changes observable behavior.

## Both dialects

`ppc` accepts the modern 1.0 grammar and the 0.4 migration dialect, in the
same project and even in the same file. They lower to one AST, so nothing after
the parser can tell which was used.

```
fn double(n: int) -> int {        craft double(n as int) gives int:
    return n * 2;                     give n * 2
}                                 done

launch { say(double(21)); }       launch:
                                      say double(21)
                                  done
```

## Layout

```
include/ppc/     headers, mirroring src/
src/support/     spans, source manager, diagnostics, arena, interning
src/syntax/      lexer, AST, parser
src/sema/        type system, builtins, checker (resolution, inference,
                 monomorphization, ownership, exhaustiveness)
src/mir/         control-flow IR, builder, optimizer passes
src/codegen/     C, native x86-64, and bytecode backends, plus the VM
src/driver/      CLI, module loader, pipeline, toolchain and cache
../runtime/      ABI 1 runtime, HTTPS, GUI, and platform sources
../stdlib/       canonical standard library modules
tests/           regression suite and runner
docs/            architecture and the full diagnostic reference
```

## Inspecting the pipeline

Each stage can be dumped, which is the fastest way to understand a
miscompilation or an unexpected diagnostic:

```sh
./ppc emit-tokens f.pp     # token stream, including newline-derived separators
./ppc emit-ast f.pp        # declarations per module
./ppc emit-hir f.pp        # typed, monomorphized functions
./ppc emit-mir -O2 f.pp    # optimized control-flow graph
./ppc emit-c f.pp          # the backend's own artifact
./ppc build --stats -O2 f.pp   # what the optimizer changed
./ppc build --time-passes f.pp # where compile time went
```

## Collections

`List<T>` holds any element type, including structs and enums:

```punpun
let tokens = list<Token>();
list_push(tokens, Token::Number(42));
say(list_size(tokens));
```

Value semantics hold: pushing an aggregate copies it in, reading copies it out.
`nums` remains the int-specific list. See `examples/token_list.pp`.

## Editor support

```sh
ppc serve --stdio
```

An LSP server built on the compiler's own semantic information — real
diagnostics with their `E####` codes, hover types, document symbols, and
semantic completion. See `docs/language-service.md` for what is and is not
implemented.

## Requirements

A C++20 compiler and a C11 compiler. The native backend needs an assembler and
linker. HTTPS loads the system libcurl at runtime; GUI loads Win32 or system X11
at runtime, so neither library adds a compiler build dependency.

## Status

The front end, checker, optimizer, and all three backends are implemented and
tested on Linux x86-64. Known gaps are the per-backend limitations above,
stack-passed arguments in the native backend, and the intentionally limited LSP
surface documented in `docs/language-service.md`. Other platforms are only
claimed after their real workflows pass.
