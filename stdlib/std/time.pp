// Small monotonic-time helpers. clock_ms() is the runtime primitive.

fn elapsed_ms(start: i64) -> i64 {
    return clock_ms() - start;
}

fn deadline_reached(deadline: i64) -> bool {
    return clock_ms() >= deadline;
}

fn deadline_after_ms(duration: i64) -> i64 {
    assert(duration >= 0, "deadline_after_ms requires a nonnegative duration");
    return clock_ms() + duration;
}

fn delay_ms(duration: i64) -> void {
    sleep_ms(duration);
}
