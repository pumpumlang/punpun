# Objects, value types, and contracts

PunPun deliberately separates `struct` and `object`.

- `struct`: value semantics, inline/stack-friendly representation, static
  dispatch by default.
- `object`: native allocated identity/reference payload, constructors,
  visibility, fields and methods.
- `contract`: compile-time interface/conformance declaration. Current 0.5
  conformance is static; dynamic interface values/vtables are future work.

Objects do not carry a reflection database by default. Ordinary method calls on
known concrete types are statically resolved by the compiler.

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

    public fn hp() -> i64 {
        return self.health;
    }
}
```
