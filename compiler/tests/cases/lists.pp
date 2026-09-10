launch {
    let values = numbers();
    for i in 0..5 { push(values, (5 - i) * 3); }
    say(size(values));
    say(values[0]);
    sort(values);
    for i in 0..size(values) { print(values[i]); print(" "); }
    say("");
    say(pop(values));
    let literal = [4, 1, 9];
    sort(literal);
    say(literal[0] + literal[1] + literal[2]);
    let window = view(literal, 1, 3);
    say(slice_len(window));
    say(slice_get(window, 0));
}
