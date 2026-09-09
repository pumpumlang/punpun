# PunPun structured async helpers.
#
# `async fn` / `await` are compiler features. This module provides reusable
# task helpers while the runtime keeps task creation lazy for ordinary programs.

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

async fn read_text_async(path: String) -> String {
    return read_text(path);
}

async fn write_text_async(path: String, value: String) {
    write_text(path, value);
}

# Cooperative workers should check cancelled() at natural loop boundaries.
# sleep_ms() is itself a cancellation safe point and returns early when the
# current task receives a cancellation request.
async fn cancellable_delay_ms(delay: i64) -> bool {
    sleep_ms(delay);
    return cancelled();
}
