# Self-hosting assessment

You asked for self-hosting. This document is an honest assessment of the gap,
based on probing the current language rather than guessing, plus an ordered plan.

**PPC cannot be written in PunPun today.** Nothing here claims otherwise.

## What was probed

Four probes were run against the current compiler. Results, not predictions:

| probe | result |
|---|---|
| `enum Expr { Num(int), Add(Expr, Expr) }` | **crashed the compiler** — now a clean E0901 |
| `struct List<T>` instantiated at `str` | works |
| character inspection via `slice(s, i, i+1)` | works |
| growable collection of non-int values | **did not exist — now implemented** |

The recursive-type probe found a genuine bug: `TypeContext::is_copy` recursed
forever and overflowed the stack. That is now fixed and pinned by
`tests/cases/err_recursive_type.pp`, and it is the most valuable thing this
assessment produced.

## The four blockers

Ordered by how much they block, largest first.

### 1. ~~No generic collection~~ — RESOLVED

`List<T>` now exists and works for every element type, including value structs
and enums, on all three backends. `examples/token_list.pp` builds exactly the
structure a lexer needs — a growable list of tagged tokens — and all three
backends produce identical output.

`nums` remains as the int-specific list for source compatibility.

Still missing: `Map<str, V>`. A compiler is mostly symbol tables, so this is now
the largest remaining collection gap.

### 2. Recursive data types

An AST is recursive by nature. PPC now rejects recursive types with a clear
diagnostic instead of crashing, but rejecting is not supporting.

The fix is known: box the recursive payload. The boxed backends (native,
bytecode) already represent enums as heap blocks and could support it almost
immediately; the C backend stores payloads in a union by value and needs the
recursive field emitted as a pointer.

### 3. No first-class functions

No closures, no function values, no dispatch tables. Much of a compiler can be
written without them — visitor dispatch becomes `match` on an enum, which PunPun
does well — so this is a real cost but not a hard blocker.

### 4. Missing I/O surface

No directory iteration, so a compiler could not discover modules. `read_text`
and `write_text` exist, which covers reading sources and writing output.

## What already works in PPC's favour

Worth stating, because the gap is narrower than the blocker list suggests:

- Generics with monomorphization, including over `str` and user types.
- Algebraic enums with exhaustiveness checking — the natural shape for tokens,
  AST node kinds, and IR instructions.
- Ownership and borrow checking, which a compiler's arena-heavy allocation
  patterns benefit from.
- Structured diagnostics, string handling, and file reading.
- Three backends, so a PunPun-written compiler could be bootstrapped through the
  bytecode VM and then compiled natively by itself.

## Ordered plan

1. ~~**`List<T>` in the runtime.**~~ Done.
2. **`Map<str, V>`.** Symbol tables, interners, module caches — a compiler is
   mostly maps.
3. **Recursive types via boxing.** Native and bytecode first, since they already
   box; then the C backend with pointer emission for recursive fields.
4. **Directory iteration**, for module discovery.
5. **Write a PunPun lexer for PunPun**, in-tree, as an executable measure of
   progress rather than a claim. Compile it with PPC and diff its token stream
   against `ppc emit-tokens`. That is a self-checking milestone.
6. Parser, then checker, then a bytecode backend — each validated the same way,
   by differential comparison against the C++ implementation.
7. **Bootstrap.** PunPun-PPC compiled by C++-PPC compiles itself; the two
   outputs must be identical.

Steps 1-4 are PunPun 1.4 library work. Step 5 is the first honest milestone and
would make the remaining distance measurable instead of estimated.

## Recommendation

Self-hosting is the right long-term goal and still the wrong immediate one, but
the gap is now one item narrower.

With `List<T>` in place the remaining blockers are `Map<str, V>`, recursive
types, and directory iteration. All three are 1.4 ecosystem work with value
independent of self-hosting.

The concrete next step is `Map<str, V>`, not a compiler rewrite.
