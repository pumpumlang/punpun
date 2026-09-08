# Objects, Structs, and Contracts

`struct` is the value-oriented representation. `object` carries reference identity and constructor/method behavior.

```punpun
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

    public fn hp() -> i64 { return self.health; }
}
```

Contract conformance is checked at compile time. Current beta method calls use static dispatch when the concrete type is known. Full runtime contract/vtable dispatch is not yet advertised as complete.
