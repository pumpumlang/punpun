# Language Basics

PunPun 0.5 uses structured braces and semicolons while retaining distinctive entry/import vocabulary.

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
