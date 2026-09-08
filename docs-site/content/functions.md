# Functions

Functions let you name behavior, declare inputs, and make return values explicit.

## Overview

Declare a function with `fn`. Parameters use `name: Type`, and a return type follows `->`. Use `return` when the function produces a value.

## Syntax

```punpun
fn add(left: i64, right: i64) -> i64 {
    return left + right;
}
```

Call it like any other expression:

```punpun
let total = add(20, 22);
```

## Runnable example

```punpun
fn twice(value: i64) -> i64 {
    return value * 2;
}

fn greet(name: String) {
    say("Hello " + name);
}

launch {
    greet("learner");
    say(twice(21));
}
```

Save as `functions.pp` and run `pp run functions.pp`.

## Common mistakes

- Returning a value that does not match the declared return type.
- Forgetting `-> Type` on a function that returns a value.
- Mutating an argument just because its name looks mutable. Mutability and references are explicit in PunPun.

## Next steps

Continue to [Control Flow](control-flow.html) for `if`, loops, and choosing what code executes.
