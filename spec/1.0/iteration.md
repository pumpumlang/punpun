# Iteration protocol

This document defines iteration behavior added within the stable 1.x language
epoch. It does not change the runtime ABI or the representation of existing
sequence types.

## `for` evaluation

`for item in expression { ... }` evaluates `expression` exactly once. Built-in
`nums`, `List<T>`, and `Slice<T>` values use their existing indexed lowering.
Other values may participate through the structural iterator protocol below.

## Structural iterable protocol

A user-defined iterable has a zero-argument method:

```punpun
fn iter() -> IteratorType
```

`IteratorType` must provide a zero-argument method:

```punpun
fn advance() -> Option<T>
```

`T` is the loop variable type. `Option::Some(value)` runs one iteration with
`value` bound to the loop variable. `Option::None` terminates the loop.

An iterator may be used directly in a `for` loop without an `iter()` wrapper
when the value itself provides `advance() -> Option<T>`.

The protocol is structural on purpose: implementing it requires no declaration
or compiler-owned base type, and existing types are unaffected. Protocol methods
may use generic parameters from their owning concrete type, but `iter()` and
`advance()` themselves must not introduce method-level generic parameters because
the loop has no call arguments from which to infer those parameters.

## Laziness and control flow

`advance()` is called once at the start of each attempted iteration, so an
iterator may compute values lazily and need not have a finite length. `break`
stops without another call to `advance()`. `continue` begins the next attempted
iteration and therefore calls `advance()` again.

## Ownership

The iterable expression is evaluated into a compiler-owned local. Copy values
are copied as usual; move-only source bindings are moved into the loop. Iterator
objects returned by `iter()` remain alive for the loop's lexical lifetime.

Stateful iterators should currently be identity `object`s. A value `struct`
iterator whose `advance` method requires `mut self` is rejected with `E0901`
until mutable value-struct borrows use an equivalent representation in the C,
direct-native, and bytecode backends. This restriction prevents a source program
from changing behavior with `--backend`.

## Backend equivalence

The protocol is lowered in semantic HIR into ordinary calls, `Option<T>` tag and
payload operations, and a `while` loop. Backends do not implement a separate
iterator opcode. All supported backends must therefore agree on observable
iteration order, termination, and element values.
