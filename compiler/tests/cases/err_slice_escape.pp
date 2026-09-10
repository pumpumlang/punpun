fn leak() -> Slice<int> {
    let values = numbers();
    push(values, 1);
    return view(values, 0, 1);
}
launch { say(slice_len(leak())); }
