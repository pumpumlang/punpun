fn text_starts_with(value: String, prefix: String) -> bool {
    return len(prefix) <= len(value) && slice(value, 0, len(prefix)) == prefix;
}

fn text_ends_with(value: String, suffix: String) -> bool {
    return len(suffix) <= len(value) && slice(value, len(value) - len(suffix), len(value)) == suffix;
}

fn text_trim(value: String) -> String {
    let mut start = 0;
    let mut end = len(value);
    while start < end && contains(" \t\r\n", slice(value, start, start + 1)) {
        start = start + 1;
    }
    while end > start && contains(" \t\r\n", slice(value, end - 1, end)) {
        end = end - 1;
    }
    return slice(value, start, end);
}

fn text_repeat(value: String, count: i64) -> String {
    assert(count >= 0, "text_repeat requires a nonnegative count");
    let mut result = "";
    for i in 0..count {
        result = concat(result, value);
    }
    return result;
}

fn text_is_utf8(value: String) -> bool { return utf8_valid(value); }
fn text_codepoints(value: String) -> i64 { return utf8_len(value); }
