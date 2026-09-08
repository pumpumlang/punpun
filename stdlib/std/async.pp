# PunPun native task helpers. `async fn` itself is a compiler feature; this
# module provides small reusable helpers without pulling an event loop into
# programs that do not use them.
async fn delay_ms(delay: i64) {
    sleep_ms(delay);
}

async fn delayed_i64(value: i64, delay: i64) -> i64 {
    sleep_ms(delay);
    return value;
}

async fn delayed_text(value: String, delay: i64) -> String {
    sleep_ms(delay);
    return value;
}
