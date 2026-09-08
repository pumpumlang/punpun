# Examples

The `.pp` sources cover current syntax and retained migration examples. Commands below assume the repository
root and a built toolchain (`make`). `go` and `run` both compile before executing;
`--` separates compiler options from program arguments.

| Entry | Demonstrates |
| --- | --- |
| [`hello/main.pp`](hello/main.pp) | Local `bring math`, recursion, mutable accumulation, scalar printing |
| [`showcase.pp`](showcase.pp) | Nested value records, shared lists, CLI/file text, ranges, statistics, standard-library imports |
| [`algorithms/sieve.pp`](algorithms/sieve.pp) | Sieve of Eratosthenes, reusable integer-list storage, checked CLI input, monotonic timing |
| [`packages/app/src/main.pp`](packages/app/src/main.pp) | A package importing a local `greeter` dependency |
| [`generics-and-results.pp`](generics-and-results.pp) | Generic structs/methods, `Option`, `Result`, matching and fallback helpers |

Run the 0.6 generics and results example with either backend:

```sh
build/ppc run examples/generics-and-results.pp
build/ppc run examples/generics-and-results.pp --cc-backend
```

`hello/math.pp` and `packages/greeter/src/main.pp` are library modules, not
standalone entry points. They are checked as part of the programs importing them.

## Hello

```sh
build/ppc go examples/hello/main.pp
```

Prints `PunPun`, Fibonacci at `10` (`55`), the sum from `1` through `100`
(`5050`), an absolute value (`42`), and the language name's byte length (`6`).
The recursive Fibonacci function illustrates syntax, not an efficient algorithm
for large inputs. `sum_to` expects a small enough input for its checked arithmetic.

## Showcase

```sh
build/ppc go examples/showcase.pp --release
build/ppc go examples/showcase.pp --release -- README.md
```

The sensor report combines `std.math`, `std.stats`, and `std.text`. It verifies
that changing a copied nested record leaves the original scalar fields alone,
while mutations through a copied `nums` handle affect both records. It then
prints a sorted copy, mean, range, and over-budget samples.

The seven readings total `206` ms and range from `17` to `61` ms. With a `40` ms
budget, only the `61` ms reading exceeds the threshold. The sorted copy contains
`17`, `19`, `22`, `24`, `28`, `35`, `61`.

With no arguments, the program only prints and uses built-in sample data. With
one argument, it explicitly reads that path as a notes file and displays at most
120 bytes after trimming. The preview is byte-based, so use ASCII notes if a
truncation must not split a UTF-8 character. No mode writes files or accesses the
network. Reading a nonexistent path or a file with embedded NUL panics. The
compiler itself still writes its normal native build artifact.

## Benchmark

Build once, then run the **native binary** to avoid including compiler startup
and C compilation in an external measurement:

```sh
build/ppc build examples/algorithms/sieve.pp --release
.punpun/bin/sieve
.punpun/bin/sieve 1000000 10
```

For a convenient compile-and-run invocation:

```sh
build/ppc go examples/algorithms/sieve.pp --release -- 1000000 10
```

Arguments are `[limit [rounds]]`. Defaults are `200000` and `5`. Limits must be
between `2` and `5000000`, rounds between `1` and `100`; malformed integers and
unsupported argument counts panic. The algorithm counts primes **through the
inclusive limit**, whereas each source range uses an exclusive `until` bound.
At the default limit it asserts the known count of `17984`; five rounds produce
checksum `89920`. No timing result is prescribed.

The program allocates one list of `limit + 1` integers before starting its timer.
Each round resets, marks composites, and counts primes using that same list.
There is no per-round list allocation. The timed interval includes those passes,
checked arithmetic/list operations, and checksum accumulation, but excludes
initial allocation, CLI parsing, printing, and compilation. Its millisecond
resolution can produce zero for tiny workloads. The printed count and checksum
make the computed result observable.

This is a conventional sieve with roughly `O(n log log n)` marking work and
`O(n)` storage. Flags use 64-bit integers, **not packed bits**; list capacity can
exceed its length. Memory and speed should not be compared directly with a
bit-packed sieve or a different algorithm without explaining that distinction.

For useful measurements, record hardware, compiler/toolchain versions, limits,
rounds, and release/debug mode. Repeat runs and report variation. An external
wall-clock measurement of the binary also includes allocation and output, unlike
the internal timer. Compare equivalent work and safety checks. The portable C backend uses `-O3 -flto` in release, while checked integer arithmetic
stays in the PunPun runtime; this example makes no claim to outperform C or every
other language.

## Local Packages

From `examples/packages/app`:

```sh
../../../pp check
../../../pp run --release
../../../pp tree
../../../pp clean
```

The application imports `greeter` using its `Punpun.toml` local dependency.
`tree` shows the dependency graph. `clean` explicitly removes the app's `.punpun/`
artifacts; it does not delete either package's sources. See the root README's
CLI and PPX sections before editing package metadata.
