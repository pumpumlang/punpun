import std.data.json

# Practical TOML configuration parser written in PunPun.
# Values reuse JsonValue because TOML's scalar/array/table data model overlaps
# JSON well. Keys are stored as dotted paths; this keeps lookup cheap and avoids
# hiding a second tree representation behind native code.

struct TomlParseResult {
    values: Map<JsonValue>,
    error: str,
    line: int,
    public fn ok() -> bool { return len(self.error) == 0; }
    public fn has(path: str) -> bool { return map_has(self.values, path); }
    public fn get(path: str) -> JsonValue {
        if !map_has(self.values, path) { return json_null(); }
        return map_get(self.values, path);
    }
    public fn get_string(path: str, fallback: str) -> str { return self.get(path).string_or(fallback); }
    public fn get_int(path: str, fallback: int) -> int { return self.get(path).int_or(fallback); }
    public fn get_bool(path: str, fallback: bool) -> bool { return self.get(path).bool_or(fallback); }
}

fn _toml_strip_comment(line: str) -> str {
    let mut quoted = false;
    let mut escaped = false;
    for i in 0..len(line) {
        let c = char_at(line, i);
        if quoted {
            if escaped { escaped = false; continue; }
            if c == 92 { escaped = true; continue; }
            if c == 34 { quoted = false; }
        } else {
            if c == 34 { quoted = true; }
            else if c == 35 { return trim(slice(line, 0, i)); }
        }
    }
    return trim(line);
}

fn _toml_unescape_string(source: str) -> str {
    # JSON basic strings have the same escape vocabulary we need here, so let
    # the JSON parser do the unpleasant Unicode work instead of duplicating it.
    let parsed = json_parse(source);
    if !parsed.ok() || !parsed.value.is_string() { return ""; }
    return parsed.value.string_value;
}

fn _toml_split_array(source: str) -> List<str> {
    let out = list<str>();
    let mut start = 0;
    let mut quoted = false;
    let mut escaped = false;
    let mut depth = 0;
    for i in 0..len(source) {
        let c = char_at(source, i);
        if quoted {
            if escaped { escaped = false; continue; }
            if c == 92 { escaped = true; continue; }
            if c == 34 { quoted = false; }
            continue;
        }
        if c == 34 { quoted = true; continue; }
        if c == 91 { depth = depth + 1; continue; }
        if c == 93 { depth = depth - 1; continue; }
        if c == 44 && depth == 0 {
            list_push(out, trim(slice(source, start, i)));
            start = i + 1;
        }
    }
    if start < len(source) { list_push(out, trim(slice(source, start, len(source)))); }
    else if len(trim(source)) == 0 { return list<str>(); }
    return out;
}

fn _toml_parse_value(source: str) -> JsonValue {
    let value = trim(source);
    if len(value) == 0 { return json_null(); }
    if starts_with(value, "\"") && ends_with(value, "\"") { return json_string(_toml_unescape_string(value)); }
    if value == "true" { return json_bool(true); }
    if value == "false" { return json_bool(false); }
    if starts_with(value, "[") && ends_with(value, "]") {
        let out = json_array();
        let inner = trim(slice(value, 1, len(value) - 1));
        let pieces = _toml_split_array(inner);
        for piece in pieces { list_push(out.array_value, _toml_parse_value(piece)); }
        return out;
    }
    # Underscores in numeric literals are readability separators in TOML.
    let numeric = replace(value, "_", "");
    if contains(numeric, ".") || contains(numeric, "e") || contains(numeric, "E") {
        return json_number(parse_float(numeric));
    }
    # Date/time literals are preserved as strings until a caller chooses a
    # timezone policy. Silently applying local time here would be surprising.
    if contains(value, "-") && (contains(value, "T") || contains(value, ":")) { return json_string(value); }
    return json_int(parse_int(numeric));
}

fn toml_parse(source: str) -> TomlParseResult {
    let values = map<JsonValue>();
    let lines = split(replace(source, "\r\n", "\n"), "\n");
    let mut section = "";
    for index in 0..list_size(lines) {
        let line = _toml_strip_comment(list_at(lines, index));
        if len(line) == 0 { continue; }
        if starts_with(line, "[") && ends_with(line, "]") {
            section = trim(slice(line, 1, len(line) - 1));
            if len(section) == 0 { return TomlParseResult(values, "empty TOML table name", index + 1); }
            continue;
        }
        let equal = index_of(line, "=", 0);
        if equal <= 0 { return TomlParseResult(values, "expected key = value", index + 1); }
        let key = trim(slice(line, 0, equal));
        let value_text = trim(slice(line, equal + 1, len(line)));
        if len(key) == 0 || len(value_text) == 0 { return TomlParseResult(values, "empty TOML key or value", index + 1); }
        let mut full = key;
        if len(section) > 0 { full = section + "." + key; }
        map_put(values, full, _toml_parse_value(value_text));
    }
    return TomlParseResult(values, "", 0);
}

fn toml_stringify(values: Map<JsonValue>) -> str {
    # Flat/dotted output is valid TOML and deterministic enough for generated
    # configuration. Table re-grouping is presentation, not semantics.
    let keys = map_keys(values);
    let mut out = "";
    for key in keys {
        let value = map_get(values, key);
        let encoded = json_stringify(value);
        out = out + key + " = " + encoded + "\n";
    }
    return out;
}
