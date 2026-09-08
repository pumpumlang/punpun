# Generics, Option, Result, and Match

Write one typed implementation for many concrete types, then handle optional and fallible values explicitly.

## Overview

Generics use `<T>` parameters. PunPun specializes generic code to concrete native implementations. `Option<T>` represents a value that may be absent, `Result<T,E>` represents success or failure, and `match` makes every case visible.

## Syntax

```punpun
fn identity<T: Copy>(value: T) -> T {
    return value;
}

enum Message<T> {
    Empty,
    Value(T),
}
```

A match must cover every enum variant:

```punpun
fn read(message: Message<int>) -> int {
    return match message {
        Message::Empty => 0,
        Message::Value(value) => value,
    };
}
```

## Runnable example

```punpun
fn checked(flag: bool) -> Result<int, str> {
    if flag {
        return Result::Ok(42);
    }
    return Result::Error("not ready");
}

fn use_checked(flag: bool) -> Result<int, str> {
    let value = checked(flag)?;
    return Result::Ok(value + 1);
}

launch {
    say(match use_checked(true) {
        Result::Ok(value) => value,
        Result::Error(message) => 0,
    });
}
```

Save as `results.pp` and run `pp run results.pp`.

## Common mistakes

- Leaving out an enum case. Exhaustive matching is checked at compile time.
- Using `?` in a function whose return type cannot carry the propagated failure.
- Adding generic constraints that the concrete type does not satisfy.

## Next steps

Read [Packages and PPX](packages.html) when you want to reuse code from another package. Read [Native Memory](memory.html) when ownership and references start to matter.
