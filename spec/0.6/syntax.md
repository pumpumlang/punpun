# PunPun 0.6 surface syntax

Status: frozen for implementation during the 0.6 development cycle.

## Generic declarations

Type parameters follow the declared name. Constraints use `:` and `+`.

```punpun
fn identity<T>(value: T) -> T {
    return value;
}

fn choose<T: Copy + Comparable<T>>(left: T, right: T) -> T {
    // implementation omitted
}

struct Pair<T, U> {
    first: T;
    second: U;
}
```

Inline constraints are the canonical 0.6 form. `where` is reserved but not part of the 0.6 grammar.

## Algebraic enums

```punpun
enum Message<T> {
    Empty,
    Value(T),
    Pair(T, T),
}
```

Variants are constructed with a qualified name such as `Message::Value(item)`. Variant names occupy the enum namespace rather than the surrounding module namespace.

## Matching and destructuring

```punpun
match message {
    Message::Empty => 0,
    Message::Value(value) => value,
    Message::Pair(left, right) => left + right,
}
```

`match` is an expression. Arms use `=>`; comma-separated arms are canonical. `_` is the wildcard. Bindings introduced by patterns are immutable within the arm unless explicitly rebound.

## Error propagation

Postfix `?` propagates an `Option::None` or `Result::Error(error)` from a function returning the same outer type. It never converts between `Option` and `Result` implicitly.

## Implementation gates

`0.6.0-dev.1` parses generic declaration headers and nested generic type spellings for tooling. Generic execution is enabled in Step 2. `enum`, `match` and `?` remain reserved until Step 3 and must produce a diagnostic rather than placeholder output.
