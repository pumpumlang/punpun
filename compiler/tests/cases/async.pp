async fn compute(value: i64, delay: i64) -> i64 { sleep_ms(delay); return value * 2; }
launch {
    let a = compute(10, 40);
    let b = compute(20, 40);
    say(await a + await b);
}
