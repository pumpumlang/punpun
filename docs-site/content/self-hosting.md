# Self-hosting Compiler

PunPun includes `selfhost/ppc_self.pp`, a compiler written in PunPun. It has its
own lexer, recursive-descent parser and deterministic C17 emitter.

```sh
make selfhost
pp selfhost selfhost/examples/hello.pp build/selfhost/hello.c
```

The bootstrap is verified as a fixed point: stage one compiles its own `.pp`
source, stage two repeats the same compilation, and both generated C files must
be byte-identical. The current seed supports its documented bootstrap subset;
advanced production-language constructs continue to use the C++ compiler.
