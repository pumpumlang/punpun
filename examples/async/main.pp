async fn delayed(value: i64, delay: i64) -> i64 {
    sleep_ms(delay);
    return value;
}

launch {
    let a = delayed(20, 50);
    let b = delayed(22, 50);
    say(await a + await b);
}
