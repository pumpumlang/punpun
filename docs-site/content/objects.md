# Objects, Structs, and Contracts

Use structs for value-shaped data and objects when identity, construction, and methods matter.

## Overview

A `struct` is value-oriented and stack-friendly. An `object` has identity-oriented semantics and supports constructors and methods. A `contract` states behavior a type must provide.

## Syntax

```punpun
struct Point {
    x: i64,
    y: i64,
}

contract Damageable {
    fn damage(amount: i64);
}

object Enemy meets Damageable {
    private let mut health: i64;

    public init(health: i64) {
        self.health = health;
    }

    public fn damage(amount: i64) {
        self.health -= amount;
    }

    public fn hp() -> i64 {
        return self.health;
    }
}
```

## Runnable example

```punpun
object Counter {
    private let mut value: i64;

    public init(start: i64) {
        self.value = start;
    }

    public fn add(amount: i64) {
        self.value += amount;
    }

    public fn get() -> i64 {
        return self.value;
    }
}

launch {
    let mut counter = Counter(40);
    counter.add(2);
    say(counter.get());
}
```

Save as `objects.pp` and run `pp run objects.pp`.

## Common mistakes

- Treating `struct` and `object` as interchangeable. Pick based on value semantics versus identity/method behavior.
- Accessing a `private` field from outside its object.
- Declaring a contract method but forgetting to implement it on a type that `meets` that contract.

## Next steps

Continue to [Generics, Option, Result, and Match](generics-results.html) to write reusable typed code and explicit error paths.
