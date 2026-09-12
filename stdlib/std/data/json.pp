# Full JSON values, parsing, serialization and pretty-printing in PunPun.
# The runtime supplies only strings, bytes, List and Map. JSON semantics live here.

fn json_null_kind() -> int { return 0; }
fn json_bool_kind() -> int { return 1; }
fn json_number_kind() -> int { return 2; }
fn json_string_kind() -> int { return 3; }
fn json_array_kind() -> int { return 4; }
fn json_object_kind() -> int { return 5; }

struct JsonValue {
    kind: int,
    bool_value: bool,
    number_value: float,
    string_value: str,
    array_value: List<JsonValue>,
    object_value: Map<JsonValue>,

    public fn is_null() -> bool { return self.kind == json_null_kind(); }
    public fn is_bool() -> bool { return self.kind == json_bool_kind(); }
    public fn is_number() -> bool { return self.kind == json_number_kind(); }
    public fn is_string() -> bool { return self.kind == json_string_kind(); }
    public fn is_array() -> bool { return self.kind == json_array_kind(); }
    public fn is_object() -> bool { return self.kind == json_object_kind(); }
    public fn bool_or(fallback: bool) -> bool { if self.is_bool() { return self.bool_value; } return fallback; }
    public fn number_or(fallback: float) -> float { if self.is_number() { return self.number_value; } return fallback; }
    public fn int_or(fallback: int) -> int { if self.is_number() { return whole(self.number_value); } return fallback; }
    public fn string_or(fallback: str) -> str { if self.is_string() { return self.string_value; } return fallback; }
    public fn size() -> int {
        if self.is_array() { return list_size(self.array_value); }
        if self.is_object() { return map_size(self.object_value); }
        return 0;
    }
    public fn at(index: int) -> JsonValue {
        if !self.is_array() || index < 0 || index >= list_size(self.array_value) { return json_null(); }
        return list_at(self.array_value, index);
    }
    public fn has(key: str) -> bool { return self.is_object() && map_has(self.object_value, key); }
    public fn get(key: str) -> JsonValue {
        if !self.is_object() || !map_has(self.object_value, key) { return json_null(); }
        return map_get(self.object_value, key);
    }
    public fn get_string(key: str, fallback: str) -> str { return self.get(key).string_or(fallback); }
    public fn get_int(key: str, fallback: int) -> int { return self.get(key).int_or(fallback); }
    public fn get_bool(key: str, fallback: bool) -> bool { return self.get(key).bool_or(fallback); }
    public fn push(value: JsonValue) -> JsonValue { if self.is_array() { list_push(self.array_value, value); } return self; }
    public fn put(key: str, value: JsonValue) -> JsonValue { if self.is_object() { map_put(self.object_value, key, value); } return self; }
    public fn encode() -> str { return json_stringify(self); }
    public fn pretty(indent: int) -> str { return json_pretty(self, indent); }
}
fn json_null() -> JsonValue { return JsonValue(json_null_kind(), false, 0.0, "", list<JsonValue>(), map<JsonValue>()); }
fn json_bool(value: bool) -> JsonValue { return JsonValue(json_bool_kind(), value, 0.0, "", list<JsonValue>(), map<JsonValue>()); }
fn json_number(value: float) -> JsonValue { return JsonValue(json_number_kind(), false, value, "", list<JsonValue>(), map<JsonValue>()); }
fn json_int(value: int) -> JsonValue { return json_number(decimal(value)); }
fn json_string(value: str) -> JsonValue { return JsonValue(json_string_kind(), false, 0.0, value, list<JsonValue>(), map<JsonValue>()); }
fn json_array() -> JsonValue { return JsonValue(json_array_kind(), false, 0.0, "", list<JsonValue>(), map<JsonValue>()); }
fn json_object() -> JsonValue { return JsonValue(json_object_kind(), false, 0.0, "", list<JsonValue>(), map<JsonValue>()); }

struct JsonParseResult {
    value: JsonValue,
    error: str,
    offset: int,
    public fn ok() -> bool { return len(self.error) == 0; }
}

object _JsonParser {
    public let source: str;
    public let pos: int;
    public let error: str;
    public init(source: str) { self.source = source; self.pos = 0; self.error = ""; }

    public fn fail(message: str) -> void {
        if len(self.error) == 0 { self.error = message; }
    }
    public fn skip_space() -> void {
        while self.pos < len(self.source) {
            let c = char_at(self.source, self.pos);
            if c == 32 || c == 9 || c == 10 || c == 13 { self.pos = self.pos + 1; }
            else { return; }
        }
    }
    public fn take(expected: int) -> bool {
        if self.pos < len(self.source) && char_at(self.source, self.pos) == expected {
            self.pos = self.pos + 1; return true;
        }
        return false;
    }
    public fn parse_value(depth: int) -> JsonValue {
        if depth > 256 { self.fail("JSON nesting exceeds 256 levels"); return json_null(); }
        self.skip_space();
        if self.pos >= len(self.source) { self.fail("expected a JSON value"); return json_null(); }
        let c = char_at(self.source, self.pos);
        if c == 110 { return self.parse_literal("null", json_null()); }
        if c == 116 { return self.parse_literal("true", json_bool(true)); }
        if c == 102 { return self.parse_literal("false", json_bool(false)); }
        if c == 34 { return json_string(self.parse_string()); }
        if c == 91 { return self.parse_array(depth + 1); }
        if c == 123 { return self.parse_object(depth + 1); }
        if c == 45 || (c >= 48 && c <= 57) { return self.parse_number(); }
        self.fail("unexpected byte while parsing JSON");
        return json_null();
    }
    public fn parse_literal(word: str, value: JsonValue) -> JsonValue {
        if self.pos + len(word) > len(self.source) || slice(self.source, self.pos, self.pos + len(word)) != word {
            self.fail("invalid JSON literal"); return json_null();
        }
        self.pos = self.pos + len(word);
        return value;
    }
    public fn parse_number() -> JsonValue {
        let start = self.pos;
        if self.take(45) {
            if self.pos >= len(self.source) { self.fail("incomplete JSON number"); return json_null(); }
        }
        if self.take(48) {
            if self.pos < len(self.source) {
                let digit_after_zero = char_at(self.source, self.pos);
                if digit_after_zero >= 48 && digit_after_zero <= 57 { self.fail("leading zero in JSON number"); return json_null(); }
            }
        } else {
            let digits = self.pos;
            while self.pos < len(self.source) {
                let c = char_at(self.source, self.pos);
                if c < 48 || c > 57 { break; }
                self.pos = self.pos + 1;
            }
            if self.pos == digits { self.fail("JSON number needs an integer part"); return json_null(); }
        }
        if self.take(46) {
            let fraction = self.pos;
            while self.pos < len(self.source) {
                let c = char_at(self.source, self.pos);
                if c < 48 || c > 57 { break; }
                self.pos = self.pos + 1;
            }
            if self.pos == fraction { self.fail("JSON fraction needs digits"); return json_null(); }
        }
        if self.pos < len(self.source) {
            let e = char_at(self.source, self.pos);
            if e == 101 || e == 69 {
                self.pos = self.pos + 1;
                if self.pos < len(self.source) {
                    let sign = char_at(self.source, self.pos);
                    if sign == 43 || sign == 45 { self.pos = self.pos + 1; }
                }
                let exponent = self.pos;
                while self.pos < len(self.source) {
                    let c = char_at(self.source, self.pos);
                    if c < 48 || c > 57 { break; }
                    self.pos = self.pos + 1;
                }
                if self.pos == exponent { self.fail("JSON exponent needs digits"); return json_null(); }
            }
        }
        return json_number(parse_float(slice(self.source, start, self.pos)));
    }
    public fn hex_digit(c: int) -> int {
        if c >= 48 && c <= 57 { return c - 48; }
        if c >= 65 && c <= 70 { return c - 65 + 10; }
        if c >= 97 && c <= 102 { return c - 97 + 10; }
        return -1;
    }
    public fn unicode4() -> int {
        if self.pos + 4 > len(self.source) { self.fail("incomplete JSON unicode escape"); return 0; }
        let mut value = 0;
        for i in 0..4 {
            let digit = self.hex_digit(char_at(self.source, self.pos + i));
            if digit < 0 { self.fail("invalid JSON unicode escape"); return 0; }
            value = value * 16 + digit;
        }
        self.pos = self.pos + 4;
        return value;
    }
    public fn utf8(codepoint: int) -> str {
        if codepoint <= 127 { return char_str(codepoint); }
        if codepoint <= 2047 {
            return char_str(192 | (codepoint >> 6)) + char_str(128 | (codepoint & 63));
        }
        if codepoint <= 65535 {
            return char_str(224 | (codepoint >> 12)) + char_str(128 | ((codepoint >> 6) & 63)) + char_str(128 | (codepoint & 63));
        }
        return char_str(240 | (codepoint >> 18)) + char_str(128 | ((codepoint >> 12) & 63)) + char_str(128 | ((codepoint >> 6) & 63)) + char_str(128 | (codepoint & 63));
    }
    public fn parse_string() -> str {
        if !self.take(34) { self.fail("expected JSON string"); return ""; }
        let mut out = "";
        while self.pos < len(self.source) {
            let c = char_at(self.source, self.pos); self.pos = self.pos + 1;
            if c == 34 { return out; }
            if c < 32 { self.fail("control byte in JSON string"); return ""; }
            if c != 92 { out = out + char_str(c); continue; }
            if self.pos >= len(self.source) { self.fail("incomplete JSON escape"); return ""; }
            let escaped = char_at(self.source, self.pos); self.pos = self.pos + 1;
            if escaped == 34 { out = out + "\""; }
            else if escaped == 92 { out = out + "\\"; }
            else if escaped == 47 { out = out + "/"; }
            else if escaped == 98 { out = out + char_str(8); }
            else if escaped == 102 { out = out + char_str(12); }
            else if escaped == 110 { out = out + "\n"; }
            else if escaped == 114 { out = out + "\r"; }
            else if escaped == 116 { out = out + "\t"; }
            else if escaped == 117 {
                let mut codepoint = self.unicode4();
                if len(self.error) > 0 { return ""; }
                if codepoint >= 55296 && codepoint <= 56319 {
                    if self.pos + 6 > len(self.source) || char_at(self.source, self.pos) != 92 || char_at(self.source, self.pos + 1) != 117 {
                        self.fail("high surrogate is missing a low surrogate"); return "";
                    }
                    self.pos = self.pos + 2;
                    let low = self.unicode4();
                    if low < 56320 || low > 57343 { self.fail("invalid JSON low surrogate"); return ""; }
                    codepoint = 65536 + (codepoint - 55296) * 1024 + (low - 56320);
                } else if codepoint >= 56320 && codepoint <= 57343 {
                    self.fail("unpaired JSON low surrogate"); return "";
                }
                out = out + self.utf8(codepoint);
            } else { self.fail("unknown JSON escape"); return ""; }
        }
        self.fail("unterminated JSON string");
        return "";
    }
    public fn parse_array(depth: int) -> JsonValue {
        let out = json_array(); self.pos = self.pos + 1; self.skip_space();
        if self.take(93) { return out; }
        while len(self.error) == 0 {
            list_push(out.array_value, self.parse_value(depth));
            if len(self.error) > 0 { return out; }
            self.skip_space();
            if self.take(93) { return out; }
            if !self.take(44) { self.fail("expected ',' or ']' in JSON array"); return out; }
            self.skip_space();
        }
        return out;
    }
    public fn parse_object(depth: int) -> JsonValue {
        let out = json_object(); self.pos = self.pos + 1; self.skip_space();
        if self.take(125) { return out; }
        while len(self.error) == 0 {
            if self.pos >= len(self.source) || char_at(self.source, self.pos) != 34 {
                self.fail("JSON object key must be a string"); return out;
            }
            let key = self.parse_string(); self.skip_space();
            if !self.take(58) { self.fail("expected ':' after JSON object key"); return out; }
            let value = self.parse_value(depth);
            if len(self.error) > 0 { return out; }
            map_put(out.object_value, key, value);
            self.skip_space();
            if self.take(125) { return out; }
            if !self.take(44) { self.fail("expected ',' or '}' in JSON object"); return out; }
            self.skip_space();
        }
        return out;
    }
}

fn json_parse(source: str) -> JsonParseResult {
    let parser = _JsonParser(source);
    let value = parser.parse_value(0);
    parser.skip_space();
    if len(parser.error) == 0 && parser.pos != len(source) { parser.fail("trailing bytes after JSON value"); }
    return JsonParseResult(value, parser.error, parser.pos);
}

fn json_parse_or_panic(source: str) -> JsonValue {
    let parsed = json_parse(source);
    if !parsed.ok() { panic("JSON error at byte " + text(parsed.offset) + ": " + parsed.error); }
    return parsed.value;
}

fn _json_escape(value: str) -> str {
    let mut out = "\"";
    for i in 0..len(value) {
        let c = char_at(value, i);
        if c == 34 { out = out + "\\\""; }
        else if c == 92 { out = out + "\\\\"; }
        else if c == 8 { out = out + "\\b"; }
        else if c == 12 { out = out + "\\f"; }
        else if c == 10 { out = out + "\\n"; }
        else if c == 13 { out = out + "\\r"; }
        else if c == 9 { out = out + "\\t"; }
        else if c < 32 {
            let hex = "0123456789abcdef";
            out = out + "\\u00" + char_str(char_at(hex, (c >> 4) & 15)) + char_str(char_at(hex, c & 15));
        } else { out = out + char_str(c); }
    }
    return out + "\"";
}

fn _json_number(value: float) -> str {
    if is_nan(value) || is_infinite(value) { return "null"; }
    let as_int = whole(value);
    if decimal(as_int) == value { return text(as_int); }
    return text_float(value);
}

fn json_stringify(value: JsonValue) -> str {
    if value.is_null() { return "null"; }
    if value.is_bool() { if value.bool_value { return "true"; } return "false"; }
    if value.is_number() { return _json_number(value.number_value); }
    if value.is_string() { return _json_escape(value.string_value); }
    if value.is_array() {
        let mut out = "[";
        for i in 0..list_size(value.array_value) {
            if i > 0 { out = out + ","; }
            out = out + json_stringify(list_at(value.array_value, i));
        }
        return out + "]";
    }
    let keys = map_keys(value.object_value);
    let mut out = "{";
    for i in 0..list_size(keys) {
        if i > 0 { out = out + ","; }
        let key = list_at(keys, i);
        out = out + _json_escape(key) + ":" + json_stringify(map_get(value.object_value, key));
    }
    return out + "}";
}

fn _json_indent(depth: int, width: int) -> str { return repeat(" ", depth * width); }

fn _json_pretty(value: JsonValue, width: int, depth: int) -> str {
    if !value.is_array() && !value.is_object() { return json_stringify(value); }
    if value.size() == 0 { return json_stringify(value); }
    if value.is_array() {
        let mut out = "[\n";
        for i in 0..list_size(value.array_value) {
            if i > 0 { out = out + ",\n"; }
            out = out + _json_indent(depth + 1, width) + _json_pretty(list_at(value.array_value, i), width, depth + 1);
        }
        return out + "\n" + _json_indent(depth, width) + "]";
    }
    let keys = map_keys(value.object_value);
    let mut out = "{\n";
    for i in 0..list_size(keys) {
        if i > 0 { out = out + ",\n"; }
        let key = list_at(keys, i);
        out = out + _json_indent(depth + 1, width) + _json_escape(key) + ": " + _json_pretty(map_get(value.object_value, key), width, depth + 1);
    }
    return out + "\n" + _json_indent(depth, width) + "}";
}

fn json_pretty(value: JsonValue, indent: int) -> str {
    let mut width = indent; if width < 0 { width = 0; } if width > 16 { width = 16; }
    return _json_pretty(value, width, 0);
}

fn json_equal(left: JsonValue, right: JsonValue) -> bool {
    if left.kind != right.kind { return false; }
    if left.is_null() { return true; }
    if left.is_bool() { return left.bool_value == right.bool_value; }
    if left.is_number() { return left.number_value == right.number_value; }
    if left.is_string() { return left.string_value == right.string_value; }
    if left.is_array() {
        if list_size(left.array_value) != list_size(right.array_value) { return false; }
        for i in 0..list_size(left.array_value) {
            if !json_equal(list_at(left.array_value, i), list_at(right.array_value, i)) { return false; }
        }
        return true;
    }
    if map_size(left.object_value) != map_size(right.object_value) { return false; }
    let keys = map_keys(left.object_value);
    for key in keys {
        if !map_has(right.object_value, key) || !json_equal(map_get(left.object_value, key), map_get(right.object_value, key)) { return false; }
    }
    return true;
}
