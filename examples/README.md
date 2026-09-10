# Examples

Build PPC from the repository root first:

```sh
make compiler
```

| Entry | Demonstrates |
|---|---|
| [`hello/main.pp`](hello/main.pp) | Imports, recursion, mutation, and output |
| [`generics-and-results.pp`](generics-and-results.pp) | Generic values, `Option`, `Result`, matching |
| [`showcase.pp`](showcase.pp) | Value semantics, lists, CLI/files, core stdlib |
| [`algorithms/sieve.pp`](algorithms/sieve.pp) | Checked collections and a measurable workload |
| [`https.pp`](https.pp) | Verified HTTPS status/body/error handling |
| [`gui.pp`](gui.pp) | Native GUI availability and message windows |

Run an example with any supported backend:

```sh
build/ppc run --backend=c examples/generics-and-results.pp
build/ppc run --backend=native examples/generics-and-results.pp
build/ppc run --backend=bytecode examples/generics-and-results.pp
```

The native x86-64 backend intentionally diagnoses unsupported async programs.
The C backend has full language coverage; bytecode runs in-process.

Build the sieve before timing it so compiler startup is excluded:

```sh
build/ppc build -O2 --backend=c -o build/sieve examples/algorithms/sieve.pp
build/sieve 1000000 10
```

`hello/math.pp` and the package-example dependency modules are libraries, not
standalone entry points.
