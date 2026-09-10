// Simple value formatting.

fn format_int(value: int, width: int) -> str {
    return pad_left(text(value), width, " ");
}

fn format_zero_padded(value: int, width: int) -> str {
    if value < 0 {
        return "-" + pad_left(text(0 - value), width - 1, "0");
    }
    return pad_left(text(value), width, "0");
}

/// Fixed-point rendering with `places` digits after the point.
fn format_fixed(value: float, places: int) -> str {
    if places < 0 { panic("format_fixed needs a nonnegative number of places"); }
    let mut scale = 1.0;
    for i in 0..places { scale = scale * 10.0; }
    // Rounding before splitting, so 0.999 at two places renders as 1.00 rather
    // than 0.99.
    let scaled = round(fabs(value) * scale);
    let whole_part = whole(scaled / scale);
    let fraction = whole(scaled - decimal(whole_part) * scale);
    let mut sign = "";
    if value < 0.0 { sign = "-"; }
    if places == 0 { return sign + text(whole_part); }
    return sign + text(whole_part) + "." + format_zero_padded(fraction, places);
}

fn format_percent(value: float, places: int) -> str {
    return format_fixed(value * 100.0, places) + "%";
}

/// Human-readable byte size, e.g. 1536 becomes "1.5 KB".
fn format_bytes(count: int) -> str {
    if count < 1024 { return text(count) + " B"; }
    let units = list<str>();
    list_push(units, "KB"); list_push(units, "MB");
    list_push(units, "GB"); list_push(units, "TB");
    let mut value = decimal(count) / 1024.0;
    let mut unit = 0;
    while value >= 1024.0 and unit < list_size(units) - 1 {
        value = value / 1024.0;
        unit = unit + 1;
    }
    return format_fixed(value, 1) + " " + list_at(units, unit);
}

/// Duration in milliseconds as a readable string.
fn format_duration(milliseconds: int) -> str {
    if milliseconds < 1000 { return text(milliseconds) + "ms"; }
    let seconds = milliseconds / 1000;
    if seconds < 60 { return format_fixed(decimal(milliseconds) / 1000.0, 1) + "s"; }
    let minutes = seconds / 60;
    if minutes < 60 { return text(minutes) + "m " + text(seconds % 60) + "s"; }
    return text(minutes / 60) + "h " + text(minutes % 60) + "m";
}

fn repeat_to_width(fill: str, width: int) -> str {
    if width <= 0 or len(fill) == 0 { return ""; }
    return left(repeat(fill, width / len(fill) + 1), width);
}
