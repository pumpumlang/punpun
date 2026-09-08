# Language Basics

Learn the smallest pieces of PunPun source: imports, bindings, mutability, types, calls, and the program entry block.

## Overview

`bring` imports a module. `let` creates a binding. Bindings are immutable unless you add `mut`. `launch` is the executable entry block.

## Syntax

```punpun
bring std::io;

launch {
    let name = "PunPun";
    let mut count = 1;
    count += 1;
    say(name);
    say(count);
}
```

Use an explicit type when it improves clarity:

```punpun
let attempts: i64 = 3;
let enabled: bool = true;
let title: String = "demo";
```

## Runnable example

Save this as `basics.pp`:

```punpun
launch {
    let language = "PunPun";
    let mut score: i64 = 40;
    score += 2;
    say(language);
    say(score);
}
```

Run it with `pp run basics.pp`.

## Common mistakes

- Reassigning an immutable binding. Use `let mut value = ...` only when reassignment is needed.
- Mixing old migration syntax into new code. Use `pp migrate` on older files instead of learning both forms at once.
- Guessing a type name. Let local inference work first, then add an explicit type when you actually need one.

## Next steps

Continue to [Functions](functions.html) to move repeated work into named, typed operations.
