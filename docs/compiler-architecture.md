# PunPun 1.3 compiler architecture

PPC is the canonical PunPun compiler. It is a C++20 program with a C11 runtime
and one shared frontend for every backend and editor query.

```text
source -> SourceManager -> lexer -> parser/AST -> checker/HIR -> MIR builder
                                                               |
                                                          optimizer
                                                               |
                                     +-------------------------+----------------+
                                     |                         |                |
                                  C backend              x86-64 backend    bytecode/VM
```

## Frontend

`compiler/src/syntax` accepts the modern stable grammar and the retained
migration dialect. `compiler/src/sema` performs declaration collection, type
resolution, generic specialization, exhaustiveness, ownership, and borrow
checks. It emits typed, resolved HIR so backends do not repeat name lookup or
language semantics.

## MIR and optimization

`compiler/src/mir` lowers HIR into an explicit control-flow graph. `-O1` runs a
cheap cleanup sweep; `-O2` iterates constant folding, propagation, branch
simplification, dead-code removal, and block cleanup to a fixed point. Checked
arithmetic is preserved: operations that would trap are not folded into wrapped
values.

## Backends

- `c` emits a C11 translation unit and invokes the host C compiler. It is the default and has full language coverage.
- `native` emits System V x86-64 assembly directly. It diagnoses unsupported async and stack-passed argument cases instead of silently changing behavior.
- `bytecode` emits register bytecode for the bundled VM. It runs in-process and uses the same runtime services as native executables.

All three consume the same MIR. The compiler regression runner compares their
observable output and uses explicit skip metadata for documented backend gaps.

## Runtime

`runtime/ppcrt.c` and the platform files implement ABI epoch 1. HTTPS is in
`runtime/ppc_https.c`; GUI support is in `runtime/ppc_gui.c`. They are linked by
the C/native backends and directly called by the VM, so there is one behavior
and error model.

Runtime object caching is content-addressed by compiler version, ABI/cache
epoch, target, compiler identity, headers, flags, and source contents. Objects
are staged and atomically renamed so concurrent builds never consume a partial
file. `--no-cache` rebuilds runtime objects in private temporary files.

## Language service

`compiler/src/service` wraps the compiler's source, syntax, and semantic data.
`ppc serve --stdio` provides diagnostics, full-text synchronization, completion,
hover, partial definitions, and nested document symbols. The VS Code extension
is a protocol client only; it does not contain a second PunPun parser.

## Layout

| Path | Responsibility |
|---|---|
| `compiler/include/ppc` | Compiler interfaces |
| `compiler/src/support` | Source management, host abstraction, diagnostics |
| `compiler/src/syntax` | Lexer and parser |
| `compiler/src/sema` | Types, builtins, ownership, HIR |
| `compiler/src/mir` | CFG IR and optimizer |
| `compiler/src/codegen` | C, x86-64, bytecode, and VM |
| `compiler/src/service` | Language service and LSP |
| `compiler/src/driver` | CLI, imports, cache, and host toolchain |
| `runtime` | ABI 1 runtime and native libraries |
| `stdlib` | PunPun source modules |
| `compiler/tests` | Cross-backend and LSP regressions |

See [the compiler implementation notes](../compiler/docs/architecture.md) and
[known limitations](../compiler/docs/known-limitations.md) for deeper detail.
