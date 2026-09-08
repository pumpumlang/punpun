# Language Basics

PunPun 0.6 development uses structured braces and semicolons while retaining distinctive entry/import vocabulary.

```punpun
bring std::math;

fn twice(value: i64) -> i64 {
    return value * 2;
}

launch {
    let mut total = 0;
    for i in 0..5 {
        total += twice(i);
    }
    say(total);
}
```

`let` is immutable by default. Add `mut` when reassignment is required. The 0.5 beta compiler still accepts selected legacy syntax for migration; `pp migrate` converts common 0.4 forms.

## Generics and algebraic enums

```punpun
fn identity<T: Copy>(value: T) -> T { return value; }

enum Message<T> {
    Empty,
    Value(T),
}

fn read(message: Message<int>) -> int {
    return match message {
        Message::Empty => 0,
        Message::Value(value) => identity(value),
    };
}
```

Generic calls are specialized to concrete native implementations. Matches must
cover every enum variant; bindings and nested variant patterns destructure
payloads. `Option<T>` and `Result<T,E>` are prelude enums, and postfix `?`
propagates their failure variant from a compatible function.
