// String helpers built on the text builtins.

fn is_empty(text_value: str) -> bool { return len(text_value) == 0; }
fn is_blank(text_value: str) -> bool { return len(trim(text_value)) == 0; }

fn char_code(text_value: str, index: int) -> int { return char_at(text_value, index); }

fn substring(text_value: str, start: int, count: int) -> str {
    let length = len(text_value);
    let mut begin = start;
    if begin < 0 { begin = 0; }
    if begin > length { begin = length; }
    let mut to = begin + count;
    if to > length { to = length; }
    if to < begin { to = begin; }
    return slice(text_value, begin, to);
}

fn left(text_value: str, count: int) -> str { return substring(text_value, 0, count); }

fn right(text_value: str, count: int) -> str {
    let length = len(text_value);
    let mut begin = length - count;
    if begin < 0 { begin = 0; }
    return slice(text_value, begin, length);
}

fn reverse(text_value: str) -> str {
    // Byte-wise: reversing UTF-8 by bytes would corrupt multi-byte scalars, so
    // this is only correct for ASCII. Callers handling arbitrary text should
    // work at the scalar level instead.
    let mut out = "";
    let length = len(text_value);
    for i in 0..length {
        out = out + char_str(char_at(text_value, length - 1 - i));
    }
    return out;
}

fn count_occurrences(text_value: str, part: str) -> int {
    if len(part) == 0 { return 0; }
    let mut total = 0;
    let mut position = index_of(text_value, part, 0);
    while position >= 0 {
        total = total + 1;
        position = index_of(text_value, part, position + len(part));
    }
    return total;
}

fn trim_start(text_value: str) -> str {
    let length = len(text_value);
    let mut start = 0;
    while start < length and is_space(char_at(text_value, start)) { start = start + 1; }
    return slice(text_value, start, length);
}

fn trim_end(text_value: str) -> str {
    let mut end = len(text_value);
    while end > 0 and is_space(char_at(text_value, end - 1)) { end = end - 1; }
    return slice(text_value, 0, end);
}

fn is_space(code: int) -> bool {
    return code == 32 or code == 9 or code == 10 or code == 13;
}

fn is_digit(code: int) -> bool { return code >= 48 and code <= 57; }
fn is_upper(code: int) -> bool { return code >= 65 and code <= 90; }
fn is_lower(code: int) -> bool { return code >= 97 and code <= 122; }
fn is_alpha(code: int) -> bool { return is_upper(code) or is_lower(code); }
fn is_alnum(code: int) -> bool { return is_alpha(code) or is_digit(code); }
fn is_hex_digit(code: int) -> bool {
    return is_digit(code) or (code >= 97 and code <= 102) or (code >= 65 and code <= 70);
}

fn capitalize(text_value: str) -> str {
    if len(text_value) == 0 { return text_value; }
    return to_upper(left(text_value, 1)) + to_lower(slice(text_value, 1, len(text_value)));
}

fn title_case(text_value: str) -> str {
    let words = split(text_value, " ");
    let out = list<str>();
    for i in 0..list_size(words) { list_push(out, capitalize(list_at(words, i))); }
    return join(out, " ");
}

fn starts_with_any(text_value: str, options: List<str>) -> bool {
    for i in 0..list_size(options) {
        if starts_with(text_value, list_at(options, i)) { return true; }
    }
    return false;
}

fn split_lines(text_value: str) -> List<str> {
    // Normalize CRLF first, so a file written on Windows does not leave a
    // stray carriage return at the end of every line.
    return split(replace(text_value, "\r\n", "\n"), "\n");
}

fn split_whitespace(text_value: str) -> List<str> {
    let out = list<str>();
    let mut current = "";
    let length = len(text_value);
    for i in 0..length {
        let code = char_at(text_value, i);
        if is_space(code) {
            if len(current) > 0 { list_push(out, current); current = ""; }
        } else {
            current = current + char_str(code);
        }
    }
    if len(current) > 0 { list_push(out, current); }
    return out;
}

fn strip_prefix(text_value: str, prefix: str) -> str {
    if starts_with(text_value, prefix) {
        return slice(text_value, len(prefix), len(text_value));
    }
    return text_value;
}

fn strip_suffix(text_value: str, suffix: str) -> str {
    if ends_with(text_value, suffix) {
        return slice(text_value, 0, len(text_value) - len(suffix));
    }
    return text_value;
}

fn center(text_value: str, width: int, fill: str) -> str {
    let missing = width - len(text_value);
    if missing <= 0 { return text_value; }
    let leading = missing / 2;
    return repeat(fill, leading) + text_value + repeat(fill, missing - leading);
}

fn equals_ignore_case(left_value: str, right_value: str) -> bool {
    return to_lower(left_value) == to_lower(right_value);
}
