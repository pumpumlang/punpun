# PunPun 1.4.5 release notes

PunPun 1.4 closes three gaps that kept ordinary programs from being expressible:
behaviour could not be passed around, sequences could not be walked, and an
interface could not be held as a value. Everything here is an addition. The
language version, runtime ABI, package format and lockfile format stay at 1.0
and 1, and 0.6 source still builds.

## Functions are values

`fn(T, U) -> R` is a type. A function named without parentheses is a value, and
`fn(x: int) -> int { ... }` can be written where an expression goes.

```punpun
fn apply(g: fn(int) -> int, v: int) -> int { return g(v); }

launch {
    say(apply(fn(x: int) -> int { return x * 3; }, 14));
}
```

A function value is the callee's index in the module function table: one word,
which is what lets the C, native and bytecode backends share a single calling
sequence instead of three notions of a code address.

Function literals do not capture. A literal sees its own parameters and
module-level names, and naming a local from around it is refused with an error
that says so. Capturing needs an environment that owns the captured values, and
the ownership rules have to define that before the syntax exists.

`sort_by(items, before)` is in the standard library — the comparator that could
not previously be handed to a sort, which is why `sort_ints` and `sort_strings`
had to be separate functions.

## Sequences iterate

```punpun
for name in names { say(name); }
```

`for` walks `nums`, `List<T>` and `Slice<T>`, lists of aggregates included. It
is rewritten in the checker into the indexed loop it replaces, so no backend
carries a second loop form.

## Contracts are types

A contract could previously constrain a generic parameter and nothing else.
Now it is a type:

```punpun
let shapes = list<Shape>();
list_push(shapes, Square(4));
list_push(shapes, Rect(3, 5));
for s in shapes { say(s.name()); }
```

A value of contract type is the object's handle, unchanged — one word, so it
fits a `List` slot, which is the point. For the handle alone to suffice, each
object now carries its type identity in a hidden leading field. Dispatch reads
that identity and selects among the types declaring they meet the contract.

## One grammar

The migration dialect warns. Each legacy form reports `W2000`, names the modern
spelling, and points at `pp migrate`. It still parses, because 1.x promised
valid 0.6 source keeps compiling; removing the forms is a major-version
decision. Warning codes now render with a `W` prefix.

The repository's own examples were split between the two grammars and have been
converted, each verified to produce identical output.

## 1.4.5 package and release integration

The stable patch release coordinates the compiler, documentation, and PPX at
one version. Linux, Arch/CachyOS, and Windows release jobs fetch the exact
`v1.4.5` PPX tag; packaging stops if that client does not match the compiler.
The Linux SDK and Arch package now actually contain the `ppx` command that
their installer and validator promise.

For projects with a `Punpun.toml`, `pp build`, `pp run`, and `pp check` obtain
the materialized package roots from PPX automatically. `ppx outdated` now
reports current and latest versions instead of its earlier placeholder answer.

## Known limitations

- Function literals cannot capture their surroundings.
- Contract dispatch is a comparison chain, so it is linear in the number of
  implementors at each call site. A jump table is the next step.
- `for` does not walk `Map<V>` or the characters of a `str`.
- Windows is release-smoke-tested in CI; macOS remains untested.

## Validation

118 compiler cases across all three backends, plus PPX dependency-path and
archive regressions, the self-host bootstrap fixed point, the ABI gate, the
backend compatibility matrix, async stress, frontend mutation fuzzing,
documentation link checking, the privacy audit, Linux release assembly,
Arch/CachyOS package install-upgrade-remove validation, and Windows compiler,
installer, file-association, upgrade, and uninstall smoke tests.
