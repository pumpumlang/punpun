# Native Memory

Safe references and raw pointers are different types. Raw pointer creation/dereference/arithmetic requires `unsafe`.

```punpun
fn bump(value: &mut i64) {
    *value = *value + 1;
}

launch {
    let mut value = 41;
    bump(&mut value);

    unsafe {
        let raw: *i64 = &raw value;
        *raw = 43;
    }
}
```

The semantic analyzer rejects mutable references to immutable bindings and simple escaping-local-reference cases. The lifetime/move analysis is a beta foundation, not yet a complete Rust-style borrow checker.
