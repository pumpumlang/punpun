// Terminal output with ANSI styling.
//
// Colour codes are written unconditionally. Detecting whether the output is a
// terminal needs an isatty builtin the runtime does not expose, so a caller
// redirecting to a file should use the plain variants. That is stated rather
// than guessed at, because emitting escape codes into a log file is worse than
// emitting none.

fn escape(code: str) -> str { return char_str(27) + "[" + code + "m"; }

fn reset() -> str { return escape("0"); }
fn bold(value: str) -> str { return escape("1") + value + reset(); }
fn dim(value: str) -> str { return escape("2") + value + reset(); }
fn italic(value: str) -> str { return escape("3") + value + reset(); }
fn underline(value: str) -> str { return escape("4") + value + reset(); }

fn red(value: str) -> str { return escape("31") + value + reset(); }
fn green(value: str) -> str { return escape("32") + value + reset(); }
fn yellow(value: str) -> str { return escape("33") + value + reset(); }
fn blue(value: str) -> str { return escape("34") + value + reset(); }
fn magenta(value: str) -> str { return escape("35") + value + reset(); }
fn cyan(value: str) -> str { return escape("36") + value + reset(); }
fn gray(value: str) -> str { return escape("90") + value + reset(); }

fn on_red(value: str) -> str { return escape("41") + value + reset(); }
fn on_green(value: str) -> str { return escape("42") + value + reset(); }

/// 24-bit colour. Supported by essentially every terminal since 2016.
fn rgb(value: str, r: int, g: int, b: int) -> str {
    return escape("38;2;" + text(r) + ";" + text(g) + ";" + text(b)) + value + reset();
}

fn clear_screen() { print(char_str(27) + "[2J" + char_str(27) + "[H"); }
fn move_cursor(row: int, column: int) {
    print(char_str(27) + "[" + text(row) + ";" + text(column) + "H");
}
fn hide_cursor() { print(char_str(27) + "[?25l"); }
fn show_cursor() { print(char_str(27) + "[?25h"); }

/// A progress bar rendered as a string, so the caller decides where it goes.
fn progress_bar(fraction: float, width: int) -> str {
    let mut clamped = fraction;
    if clamped < 0.0 { clamped = 0.0; }
    if clamped > 1.0 { clamped = 1.0; }
    let filled = whole(clamped * decimal(width));
    return "[" + repeat("#", filled) + repeat("-", width - filled) + "]";
}
