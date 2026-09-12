# Small backtracking regular-expression engine implemented in PunPun.
# Supported: literals, ., ^, $, escapes (\d \w \s), character classes/ranges,
# negated classes, and greedy *, +, ?. Search/replace/split are built on it.

struct RegexMatch {
    matched: bool,
    start: int,
    end: int,
    text: str,
}

fn regex_no_match() -> RegexMatch { return RegexMatch(false, -1, -1, ""); }

fn _regex_class_end(pattern: str, start: int) -> int {
    let mut i = start + 1;
    if i < len(pattern) && char_at(pattern, i) == 94 { i = i + 1; }
    while i < len(pattern) {
        if char_at(pattern, i) == 92 { i = i + 2; continue; }
        if char_at(pattern, i) == 93 { return i; }
        i = i + 1;
    }
    return -1;
}

fn _regex_named_class(kind: int, c: int) -> bool {
    if kind == 100 { return c >= 48 && c <= 57; } # d
    if kind == 115 { return c == 32 || c == 9 || c == 10 || c == 13; } # s
    if kind == 119 { return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 95; } # w
    return c == kind;
}

fn _regex_class_matches(pattern: str, start: int, finish: int, c: int) -> bool {
    let mut i = start + 1;
    let mut negate = false;
    if i < finish && char_at(pattern, i) == 94 { negate = true; i = i + 1; }
    let mut matched = false;
    while i < finish {
        let mut low = char_at(pattern, i);
        if low == 92 && i + 1 < finish {
            i = i + 1; low = char_at(pattern, i);
            if low == 100 || low == 115 || low == 119 { if _regex_named_class(low, c) { matched = true; } i = i + 1; continue; }
        }
        if i + 2 < finish && char_at(pattern, i + 1) == 45 {
            let high = char_at(pattern, i + 2);
            if c >= low && c <= high { matched = true; }
            i = i + 3;
        } else {
            if c == low { matched = true; }
            i = i + 1;
        }
    }
    if negate { return !matched; }
    return matched;
}

fn _regex_atom_end(pattern: str, at: int) -> int {
    if at >= len(pattern) { return at; }
    if char_at(pattern, at) == 91 {
        let end = _regex_class_end(pattern, at);
        if end < 0 { return at + 1; }
        return end + 1;
    }
    if char_at(pattern, at) == 92 && at + 1 < len(pattern) { return at + 2; }
    return at + 1;
}

fn _regex_atom_matches(pattern: str, at: int, atom_end: int, text_value: str, pos: int) -> bool {
    if pos >= len(text_value) { return false; }
    let token = char_at(pattern, at);
    let c = char_at(text_value, pos);
    if token == 46 { return c != 10; }
    if token == 91 { return _regex_class_matches(pattern, at, atom_end - 1, c); }
    if token == 92 && at + 1 < atom_end { return _regex_named_class(char_at(pattern, at + 1), c); }
    return token == c;
}

fn _regex_match_from(pattern: str, p: int, text_value: str, t: int, depth: int) -> int {
    if depth > 2048 { return -1; }
    if p >= len(pattern) { return t; }
    if char_at(pattern, p) == 36 && p + 1 == len(pattern) { if t == len(text_value) { return t; } return -1; }

    let atom_end = _regex_atom_end(pattern, p);
    let mut quantifier = 0;
    if atom_end < len(pattern) {
        let q = char_at(pattern, atom_end);
        if q == 42 || q == 43 || q == 63 { quantifier = q; }
    }
    let mut after = atom_end;
    if quantifier != 0 { after = atom_end + 1; }

    if quantifier == 63 {
        if _regex_atom_matches(pattern, p, atom_end, text_value, t) {
            let used = _regex_match_from(pattern, after, text_value, t + 1, depth + 1);
            if used >= 0 { return used; }
        }
        return _regex_match_from(pattern, after, text_value, t, depth + 1);
    }

    if quantifier == 42 || quantifier == 43 {
        let mut end = t;
        while _regex_atom_matches(pattern, p, atom_end, text_value, end) { end = end + 1; }
        let mut minimum = t;
        if quantifier == 43 { minimum = t + 1; if end < minimum { return -1; } }
        let mut candidate = end;
        while candidate >= minimum {
            let result = _regex_match_from(pattern, after, text_value, candidate, depth + 1);
            if result >= 0 { return result; }
            candidate = candidate - 1;
        }
        return -1;
    }

    if !_regex_atom_matches(pattern, p, atom_end, text_value, t) { return -1; }
    return _regex_match_from(pattern, after, text_value, t + 1, depth + 1);
}

fn regex_search_from(pattern: str, text_value: str, offset: int) -> RegexMatch {
    let mut start = offset;
    if start < 0 { start = 0; }
    if starts_with(pattern, "^") {
        if start > 0 { return regex_no_match(); }
        let end = _regex_match_from(pattern, 1, text_value, 0, 0);
        if end >= 0 { return RegexMatch(true, 0, end, slice(text_value, 0, end)); }
        return regex_no_match();
    }
    while start <= len(text_value) {
        let end = _regex_match_from(pattern, 0, text_value, start, 0);
        if end >= 0 { return RegexMatch(true, start, end, slice(text_value, start, end)); }
        start = start + 1;
    }
    return regex_no_match();
}

fn regex_search(pattern: str, text_value: str) -> RegexMatch { return regex_search_from(pattern, text_value, 0); }
fn regex_is_match(pattern: str, text_value: str) -> bool { return regex_search(pattern, text_value).matched; }
fn regex_full_match(pattern: str, text_value: str) -> bool {
    let wrapped = "^" + pattern + "$";
    return regex_is_match(wrapped, text_value);
}

fn regex_find_all(pattern: str, text_value: str) -> List<str> {
    let out = list<str>();
    let mut pos = 0;
    while pos <= len(text_value) {
        let found = regex_search_from(pattern, text_value, pos);
        if !found.matched { break; }
        list_push(out, found.text);
        if found.end == found.start { pos = found.end + 1; } else { pos = found.end; }
    }
    return out;
}

fn regex_replace(pattern: str, text_value: str, replacement: str) -> str {
    let mut out = "";
    let mut pos = 0;
    while pos <= len(text_value) {
        let found = regex_search_from(pattern, text_value, pos);
        if !found.matched { out = out + slice(text_value, pos, len(text_value)); return out; }
        out = out + slice(text_value, pos, found.start) + replacement;
        if found.end == found.start {
            if found.end < len(text_value) { out = out + char_str(char_at(text_value, found.end)); }
            pos = found.end + 1;
        } else { pos = found.end; }
    }
    return out;
}

fn regex_split(pattern: str, text_value: str) -> List<str> {
    let out = list<str>();
    let mut pos = 0;
    while pos <= len(text_value) {
        let found = regex_search_from(pattern, text_value, pos);
        if !found.matched { list_push(out, slice(text_value, pos, len(text_value))); return out; }
        list_push(out, slice(text_value, pos, found.start));
        if found.end == found.start { pos = found.end + 1; } else { pos = found.end; }
    }
    return out;
}
