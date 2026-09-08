# Native Memory

PunPun keeps safe references separate from raw pointers and makes unsafe operations visibly opt-in.

## Overview

Use `&T` for a shared safe reference and `&mut T` when mutation through the reference is allowed. Raw pointers require an `unsafe` region. The ownership pass also tracks explicit moves, borrows, and checked slices.

## Syntax

```punpun
fn bump(value: &mut i64) {
    *value = *value + 1;
}
```

Raw-pointer work is explicit:

```punpun
unsafe {
    let pointer: *i64 = &raw value;
    *pointer = 42;
}
```

## Runnable example

```punpun
fn bump(value: &mut i64) {
    *value = *value + 1;
}

launch {
    let mut value = 40;
    bump(&mut value);
    bump(&mut value);
    say(value);
}
```

Save as `references.pp` and run `pp run references.pp`.

## Common mistakes

- Taking a mutable reference to an immutable binding.
- Using a value after an explicit `move(...)` without reinitializing it.
- Dereferencing or doing raw-pointer arithmetic outside `unsafe`.
- Mutating a list while a live checked slice borrows it.

## Next steps

Read [Compiler Errors](errors.html) to understand move/borrow diagnostics, then [Foreign Injection](injection.html) if you need native ABI integration.
