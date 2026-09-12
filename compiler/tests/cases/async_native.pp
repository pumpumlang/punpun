// Async uses the same native ABI, including stack-passed arguments and f64 results.
async fn wide_async(a: int, b: int, c: int, d: int,
                    e: int, f: int, g: int, h: int) -> int {
    sleep_ms(1);
    return a + b + c + d + e + f + g + h;
}

async fn float_async(value: float) -> float {
    sleep_ms(1);
    return value * 2.0;
}

launch {
    let wide = wide_async(1, 2, 3, 4, 5, 6, 7, 8);
    let floating = float_async(2.5);
    say(await wide);
    say(await floating);
}
