// Levelled logging to standard output.
//
// The threshold is passed in rather than held in a global, because PunPun has
// no module-level mutable state and hiding one behind a function would make the
// ordering of writes depend on initialization order.

fn level_debug() -> int { return 10; }
fn level_info() -> int { return 20; }
fn level_warn() -> int { return 30; }
fn level_error() -> int { return 40; }

fn level_name(level: int) -> str {
    if level <= level_debug() { return "DEBUG"; }
    if level <= level_info() { return "INFO"; }
    if level <= level_warn() { return "WARN"; }
    return "ERROR";
}

fn log_at(threshold: int, level: int, message: str) {
    if level < threshold { return; }
    say(pad_right(level_name(level), 5, " ") + " " + message);
}

fn debug(threshold: int, message: str) { log_at(threshold, level_debug(), message); }
fn info(threshold: int, message: str) { log_at(threshold, level_info(), message); }
fn warn(threshold: int, message: str) { log_at(threshold, level_warn(), message); }
fn error(threshold: int, message: str) { log_at(threshold, level_error(), message); }

/// A timestamped line, for output that will be read later rather than watched.
fn log_stamped(threshold: int, level: int, message: str) {
    if level < threshold { return; }
    let now = now_ms();
    let stamp = (format_two(hour_of(now)) + ":" + format_two(minute_of(now))
                 + ":" + format_two(second_of(now)));
    say(stamp + " " + pad_right(level_name(level), 5, " ") + " " + message);
}

fn format_two(value: int) -> str { return pad_left(text(value), 2, "0"); }

fn log_to_file(path: str, level: int, message: str) {
    append_text(path, level_name(level) + " " + message + "\n");
}
