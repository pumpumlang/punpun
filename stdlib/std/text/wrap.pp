// Line wrapping and indentation.

/// Wraps at `width` columns, breaking only at spaces. A word longer than the
/// width is left intact on its own line rather than split, because splitting a
/// URL or an identifier is almost never what the caller wanted.
fn wrap_text(value: str, width: int) -> List<str> {
    let lines = list<str>();
    if width <= 0 { list_push(lines, value); return lines; }

    let words = split_whitespace(value);
    let mut current = "";
    for i in 0..list_size(words) {
        let word = list_at(words, i);
        if len(current) == 0 {
            current = word;
        } else if len(current) + 1 + len(word) <= width {
            current = current + " " + word;
        } else {
            list_push(lines, current);
            current = word;
        }
    }
    if len(current) > 0 { list_push(lines, current); }
    return lines;
}

fn wrap_to_text(value: str, width: int) -> str {
    return join(wrap_text(value, width), "\n");
}

fn indent(value: str, prefix: str) -> str {
    let lines = split(value, "\n");
    let out = list<str>();
    for i in 0..list_size(lines) { list_push(out, prefix + list_at(lines, i)); }
    return join(out, "\n");
}

fn dedent(value: str) -> str {
    let lines = split(value, "\n");
    // The common indent is the smallest leading run of spaces across every
    // non-blank line; blank lines are ignored so they do not force it to zero.
    let mut smallest = -1;
    for i in 0..list_size(lines) {
        let line = list_at(lines, i);
        if len(trim(line)) == 0 { continue; }
        let mut spaces = 0;
        while spaces < len(line) and char_at(line, spaces) == 32 { spaces = spaces + 1; }
        if smallest < 0 or spaces < smallest { smallest = spaces; }
    }
    if smallest <= 0 { return value; }

    let out = list<str>();
    for i in 0..list_size(lines) {
        let line = list_at(lines, i);
        if len(line) >= smallest {
            list_push(out, slice(line, smallest, len(line)));
        } else {
            list_push(out, trim(line));
        }
    }
    return join(out, "\n");
}

fn truncate_text(value: str, width: int, ellipsis: str) -> str {
    if len(value) <= width { return value; }
    if width <= len(ellipsis) { return slice(ellipsis, 0, width); }
    return slice(value, 0, width - len(ellipsis)) + ellipsis;
}
