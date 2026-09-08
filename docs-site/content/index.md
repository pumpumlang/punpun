# PunPun

PunPun 0.5.0-beta is an ahead-of-time native programming language project focused on fast development builds, object/value types, explicit low-level access, and compiler-powered editor tooling.

## First program

```punpun
bring std::io;

launch {
    say("Hello from PunPun!");
}
```

```sh
pp new hello
cd hello
pp run
```

## What works in this beta

- Native Linux x86-64 code generation with a portable C backend for supported cross-builds.
- Content-hash build reuse and atomic executable replacement.
- Objects, structs, methods, constructors, visibility and compile-time contracts.
- Safe references, raw pointers behind `unsafe`, `sizeof` and `alignof` foundations.
- Live VS Code diagnostics through the same compiler semantic engine used by `pp check`.
- Local/path dependencies and the PPX beta package client.
- Explicit `@inject->c` native interoperability with cached foreign objects.
- A PunPun-written compiler with verified stage-one/stage-two fixed-point output.

Advanced generics, full async/await state-machine lowering, dynamic contract dispatch, and the remote PPX service remain beta work rather than claimed finished features.
