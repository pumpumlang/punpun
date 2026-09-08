# `@inject` native interoperability

PunPun normally compiles PunPun directly through its frontend, semantic model,
HIR, native backend and linker. `@inject` is an explicit escape hatch and is not
a hidden transpilation stage.

## Supported adapters

The compiler recognizes `@inject->c`, `@inject->cpp` (or `cxx`),
`@inject->rust`, and `@inject->asm`. Each adapter has a distinct cache key and
uses capability-based host compiler discovery. C, C++, and assembly are covered
by the normal Linux test suite; the Rust execution test runs when `rustc` is
available.

## C example

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

During `pp build`/`pp run` the C block is hashed and compiled once to an object
in `.punpun/cache/inject/`, then linked with PunPun code. Unchanged blocks reuse
the cached object. `pp check`, hover, completion, and formatting do not execute
or compile injected source.

The current direct x86-64 ABI bridge intentionally supports a conservative set
of scalar/pointer-compatible types and up to six scalar arguments. C-compatible
struct layout and broader ABI classification remain planned.

Injected code is intentionally separate from normal PunPun compilation. It is
never compiled during `check` or editor analysis, and projects without injection
never initialize a foreign compiler.
