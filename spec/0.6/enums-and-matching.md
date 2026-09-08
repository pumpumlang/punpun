# PunPun 0.6 enums and matching

## Algebraic enums

An enum is a closed set of variants. A variant can carry no value, one value or a tuple of values. Recursive enums require indirection through an owning/reference container; infinitely sized direct recursion is rejected.

The source-level layout is unspecified unless a future representation attribute explicitly fixes it. FFI must not assume an enum's tag width or payload layout.

## Standard option and result types

The standard library defines these ordinary algebraic enums:

```punpun
enum Option<T> {
    None,
    Some(T),
}

enum Result<T, E> {
    Ok(T),
    Error(E),
}
```

`Option<T>` is the only safe-language representation of an optional value. PunPun 0.6 has no `null` literal and no nullable suffix type. Raw pointers may represent a null address only inside APIs that explicitly document it and remain subject to `unsafe` rules.

## Patterns

Patterns may contain qualified variants, nested variant/tuple payloads, literals, immutable bindings and `_`. A binding must occur consistently in every alternative of a future or-pattern; or-patterns themselves are not enabled in 0.6.

Matching a borrowed enum borrows payload bindings. Matching an owned enum moves non-`Copy` payload bindings unless the pattern explicitly borrows them.

## Exhaustiveness and reachability

- Every enum variant must be covered unless a wildcard arm covers the remainder.
- Boolean matches must cover both values or use a wildcard.
- Integer and text matches require a wildcard because the domain is open.
- Guarded arms do not count as exhaustive coverage.
- An arm fully covered by earlier unguarded arms is unreachable and rejected.

Exhaustiveness is a compile-time property. The backend must not add a hidden normal-runtime fallback for a match proven exhaustive; corrupted discriminants may trap in debug/safety modes.

## `?` behavior

For `Result<T,E>`, `value?` yields `T` for `Ok` and returns `Error(E)` from the enclosing function. For `Option<T>`, it yields `T` for `Some` and returns `None`. The enclosing return type must have the same outer enum and compatible error type.
