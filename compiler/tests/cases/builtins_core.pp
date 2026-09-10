launch {
    let m = map<int>();
    map_put(m, "alpha", 1);
    map_put(m, "beta", 2);
    say(map_size(m));
    say(map_get(m, "beta"));
    say(map_has(m, "gamma"));
    say(map_get_or(m, "gamma", 99));
    map_remove(m, "alpha");
    say(map_size(m));

    let parts = split("a,b,c", ",");
    say(list_size(parts));
    say(join(parts, "-"));
    say(to_upper("hello"));
    say(trim("  padded  "));
    say(replace("aXbXc", "X", "."));
    say(index_of("hello world", "world", 0));
    say(starts_with("prefix", "pre"));
    say(char_at("A", 0));
    say(pad_left("7", 3, "0"));

    say(sqrt(16.0));
    say(floor(3.7));
    say(pow(2.0, 10.0));

    let b = bytes_from_text("hi");
    say(bytes_len(b));
    say(bytes_at(b, 0));
    say(bytes_to_text(b));
}
