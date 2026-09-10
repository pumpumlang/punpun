# Language service and LSP

PPC exposes its own semantic information as a queryable API, and puts a Language
Server Protocol server on top of it.

The layering is deliberate:

```
editor  ──LSP/JSON-RPC──▶  src/service/lsp_server.cpp
                                    │  translation only
PunPun 1.5 IDE  ────────────▶  LanguageService  (include/ppc/service/)
                                    │  reuses, never reimplements
                              lexer, parser, checker
```

`lsp_server.cpp` owns no language knowledge. It converts LSP requests into
`LanguageService` calls and results back into JSON. The 1.5 IDE is expected to
link `LanguageService` directly and skip the protocol entirely, while existing
editors get identical answers through LSP.

There is no separate "editor parser". A second implementation of PunPun's
semantics would drift from the compiler and give the editor different answers
than the build, which is the single worst failure mode for this kind of tool.

## Using it

```sh
ppc serve --stdio
```

Configure your editor to launch that for `.pp` files. `--stdio` is accepted for
symmetry with other language servers; stdio is currently the only transport.

## What is implemented

Only implemented capabilities are advertised in `initialize`. Advertising one
that is not wired up makes an editor show an empty result where it should show
nothing, which users read as a broken server.

| capability | status | notes |
|---|---|---|
| `publishDiagnostics` | working | real compiler diagnostics, with codes |
| `textDocumentSync` | working | full sync; unsaved buffers via overlays |
| `documentSymbol` | working | nested: fields, variants, parameters |
| `hover` | working | declaration signature, or inferred expression type |
| `completion` | working | semantic, from real declarations and builtins |
| `definition` | partial | resolves only where the index records a target |

Diagnostics carry their `E####` code, and secondary labels become LSP
`relatedInformation`, so an editor can jump from a use-after-move to the move
that caused it.

Completion is semantic, never text matching: a name appears only if it is a real
declaration in the analysed program or a builtin. Enum variants are offered
qualified (`State::Ready`), because a bare variant name would not compile.

## What is not implemented

- **References, rename, code actions, signature help, semantic highlighting.**
  Not started.
- **Definition is partial.** The index records expression types but not, in
  every case, the declaration a name resolves to, so definition succeeds only
  where that link exists.
- **Hover on a local's declaration** falls back to the enclosing function,
  because locals are not indexed as declaration symbols. Hover on a *use* of a
  local works, via the expression index.
- **Workspace-wide search.** Queries cover what was compiled from the entry
  file, not an indexed workspace.

## Performance

`LanguageService` caches one analysis per file and re-runs the front end only
when the file or an overlay changed, so repeated queries on an idle buffer cost
nothing. Batch compilation does not link or pay for any of this: `src/service/`
is only reached through `ppc serve`.

## Testing

```sh
make test-lsp
```

`tests/lsp/test_lsp.py` drives the real binary over the real protocol and checks
17 properties: framing, clean shutdown status, advertised capabilities,
diagnostic codes and positions, symbol nesting, hover content, completion
contents, and that an unknown method produces a JSON-RPC error rather than a
crash. It is also part of `make test-full`.
