# Self-hosting PunPun compiler

`ppc_self.pp` is a compiler written in PunPun. It contains its own lexer,
recursive-descent parser, type spelling/lowering rules, and deterministic C17
emitter. It does not invoke or embed the C++ compiler while translating source.

The bootstrap proves a fixed point:

1. The native C++ stage-zero compiler builds `ppc_self.pp`.
2. That PunPun-built stage-one compiler compiles its own source to C17.
3. The C17 output and PunPun runtime build the stage-two compiler.
4. Stage two compiles `ppc_self.pp` again.
5. The two generated C files must be byte-identical.

Run:

```sh
./selfhost/bootstrap.sh
```

Then compile a bootstrap-subset program:

```sh
build/selfhost/ppc-self selfhost/examples/hello.pp build/selfhost/hello.c
cc -std=c17 -Iruntime build/selfhost/hello.c runtime/libpunpun.a -pthread -lm -o build/selfhost/hello
build/selfhost/hello
```

## Bootstrap subset

The self-hosting compiler intentionally starts with a closed subset that it can
compile itself: functions, explicit scalar/string/`nums` types, `launch`, local
bindings, assignment, calls, returns, `if`/`else`, `while`, boolean/arithmetic
expressions, comments, and the runtime primitives used by the compiler. Every
local requires an explicit type.

Objects, imports, async functions, injection, floats, list literals, named or
default arguments, ownership operations, and full semantic type inference still
use the production C++ compiler. Unsupported syntax is rejected rather than
silently copied. This is a real stage-one fixed-point compiler, not yet a full
replacement for the production frontend/backend.
