# PunPun 0.5.0-beta performance notes

These are measurements, not universal performance claims. They were collected
inside an isolated Linux x86-64 build environment on 2026-09-07 after the live-LSP,
contract, implicit-self, and C-injection work in this source tree.

## Environment

- OS/kernel: Linux 6.18.35 x86_64
- CPU reported by environment: Intel Xeon Platinum 8573C (5 vCPUs exposed)
- host C compiler: GCC/cc 14.2.0
- linker: GNU ld 2.44
- PunPun compiler: 0.5.0-beta, C++17 bootstrap compiler
- native target: Linux x86-64, direct PunPun assembly backend

## Tiny program benchmark

Source:

```punpun
bring std::io;

launch {
    say("Hello from PunPun!");
}
```

Each number below is the median of repeated process-level measurements. The
measurement includes process startup. Cold build uses `--no-cache` and a fresh
output name; no-change build uses a valid existing executable/fingerprint.

| Operation | Median |
| --- | ---: |
| `ppc check main.pp` | 2.244 ms |
| cold native build | 27.176 ms |
| no-change native build | 2.582 ms |
| executable process startup + exit | 0.935 ms |

The five measured cold builds ranged from 26.484 ms to 32.807 ms. Ten measured
no-change builds ranged from 2.075 ms to 5.014 ms.

## Language-server diagnostic latency

The LSP keeps one `ppc semantic-worker` process alive. An unsaved edit containing
an unknown identifier was sent through `textDocument/didChange`, compiler
semantic analysis, and `textDocument/publishDiagnostics`.

- 10 edits
- 20 ms editor debounce
- median end-to-end diagnostic latency: **21.440 ms**
- observed range: **21.116–22.055 ms**

This benchmark does not perform machine code generation or linking.

## Explicit C injection

`@inject->c` is lazy. Projects without injection do not discover or spawn the
foreign C compiler. Injection objects are content-addressed under
`.punpun/cache/inject/`; a second build with unchanged injected source reuses the
object. The complete executable cache can additionally skip all codegen/linking
when the whole program is unchanged.

## What these numbers do not prove

They do not establish that PunPun is faster than C, C++, Rust, Zig, or assembly.
Runtime comparisons require equivalent algorithms, memory strategies, compiler
flags, and workloads. The direct backend still lacks a production register
allocator and several planned release optimizations.
