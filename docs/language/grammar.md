# PunPun 0.6 development grammar overview

PunPun 0.5 uses structured brace blocks and semicolon-terminated ordinary
statements. During the beta the compiler can still parse the 0.4 migration
dialect so existing projects can be converted with `pp migrate`.

## Modules and entry point

```punpun
bring std::io;
bring game::player;

launch {
    say("hello");
}
```

`import` and `fn main()` are also accepted, but `bring`, `launch`, and `say` are
the distinctive PunPun spellings used in project templates and documentation.

## Bindings

```punpun
let name = "PunPun";
let mut health: i64 = 100;
const MAX_PLAYERS: i64 = 32;
```

## Functions

```punpun
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}
```

Generic parameters and inline constraints follow the declared name. Calls may
infer their type arguments or spell them explicitly.

```punpun
fn identity<T: Copy>(value: T) -> T { return value; }
let first = identity(42);
let second = identity<str>("typed");
```

## Enums and matching

```punpun
enum OptionLike<T> { None, Some(T) }

fn value_or(item: OptionLike<int>) -> int {
    return match item {
        OptionLike::Some(value) => value,
        OptionLike::None => 0,
    };
}
```

Enum, boolean, integer and string patterns are supported. Enum and boolean
matches must be exhaustive; integer and string matches require `_`. Variant
payload patterns may nest. Prelude `Option<T>` and `Result<T,E>` additionally
support postfix `?` in a compatible return type.

## Control flow

```punpun
if health > 0 {
    say("alive");
} else {
    say("dead");
}

while running {
    update();
}

for i in 0..10 {
    say(i);
}
```

## Value types and objects

`struct` has value semantics. `object` has identity/reference semantics.
Instance methods may omit the receiver parameter; the parser inserts the
receiver and infers whether direct `self.field` mutation requires mutable self.
Explicit `self` / `mut self` is also accepted when the author wants to state
receiver intent.

## Contracts

```punpun
contract Damageable {
    fn damage(amount: i64);
}

object Enemy meets Damageable {
    // ...
}
```

The current contract foundation checks conformance at compile time and does not
force dynamic dispatch.

## Native memory

Safe references use `&T` / `&mut T`. Raw pointers use `*T` and operations such
as `&raw value` and raw dereference require an explicit `unsafe` block.

## Foreign injection

`@inject->c("""...""")` is explicit foreign interoperability, not the normal
PunPun compilation pipeline. See `docs/injection/README.md`.
