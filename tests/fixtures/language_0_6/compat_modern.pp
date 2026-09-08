fn double(value: i64) -> i64 {
    return value * 2;
}

launch {
    let answer = double(21);
    say(answer);
}
