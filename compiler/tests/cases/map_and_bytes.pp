// Map<V> and bytes across value kinds, including the aliasing and copy rules.
struct P { x: int }

launch {
    // Map values follow the same slot rules as List elements.
    let strs = map<str>();
    map_put(strs, "k", "v");
    say(map_get(strs, "k"));

    let reals = map<float>();
    map_put(reals, "pi", 3.5);
    say(map_get(reals, "pi") * 2.0);

    // Aggregates stored in a map keep value semantics.
    let ps = map<P>();
    let mut p = P(1);
    map_put(ps, "a", p);
    p.x = 99;
    say(map_get(ps, "a").x);

    // Overwriting a key replaces rather than duplicating.
    map_put(strs, "k", "w");
    say(map_size(strs));
    say(map_get(strs, "k"));

    // Removal, then reinsertion, must survive the tombstone.
    map_remove(strs, "k");
    say(map_has(strs, "k"));
    map_put(strs, "k", "z");
    say(map_get(strs, "k"));

    // Enough entries to force at least one rehash.
    let many = map<int>();
    for i in 0..100 { map_put(many, "key" + text(i), i); }
    say(map_size(many));
    say(map_get(many, "key57"));
    say(list_size(map_keys(many)));

    // bytes round-trips through text and slicing.
    let b = bytes_from_text("hello");
    say(bytes_to_text(bytes_slice(b, 1, 4)));
    say(bytes_len(bytes_concat(b, b)));
}
