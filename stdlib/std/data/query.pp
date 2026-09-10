// URL percent-encoding and query strings.

fn is_unreserved(code: int) -> bool {
    // RFC 3986 unreserved set: these never need escaping, and escaping them
    // anyway would produce a different-looking but equivalent URL.
    if code >= 48 and code <= 57 { return true; }
    if code >= 65 and code <= 90 { return true; }
    if code >= 97 and code <= 122 { return true; }
    return code == 45 or code == 46 or code == 95 or code == 126;
}

fn url_encode(value: str) -> str {
    let digits = "0123456789ABCDEF";
    let mut out = "";
    for i in 0..len(value) {
        let code = char_at(value, i);
        if is_unreserved(code) {
            out = out + char_str(code);
        } else {
            out = out + "%" + (slice(digits, code / 16, code / 16 + 1)
                               + slice(digits, code % 16, code % 16 + 1));
        }
    }
    return out;
}

fn url_decode(value: str) -> str {
    let mut out = "";
    let length = len(value);
    let mut i = 0;
    while i < length {
        let code = char_at(value, i);
        if code == 37 and i + 2 < length {
            let high = hex_nibble(char_at(value, i + 1));
            let low = hex_nibble(char_at(value, i + 2));
            out = out + char_str(high * 16 + low);
            i = i + 3;
        } else if code == 43 {
            // '+' means space in a form-encoded body, which is where decoded
            // query strings almost always come from.
            out = out + " ";
            i = i + 1;
        } else {
            out = out + char_str(code);
            i = i + 1;
        }
    }
    return out;
}

fn hex_nibble(code: int) -> int {
    if code >= 48 and code <= 57 { return code - 48; }
    if code >= 97 and code <= 102 { return code - 87; }
    if code >= 65 and code <= 70 { return code - 55; }
    panic("invalid percent-encoding");
    return 0;
}

fn query_encode(values: Map<str>) -> str {
    let keys = map_keys(values);
    let parts = list<str>();
    for i in 0..list_size(keys) {
        let key = list_at(keys, i);
        list_push(parts, url_encode(key) + "=" + url_encode(map_get(values, key)));
    }
    return join(parts, "&");
}

fn query_decode(query: str) -> Map<str> {
    let values = map<str>();
    if len(query) == 0 { return values; }
    let pairs = split(query, "&");
    for i in 0..list_size(pairs) {
        let pair = list_at(pairs, i);
        if len(pair) == 0 { continue; }
        let split_at = index_of(pair, "=", 0);
        if split_at < 0 {
            // A bare key with no '=' is a present-but-empty value.
            map_put(values, url_decode(pair), "");
        } else {
            map_put(values, url_decode(slice(pair, 0, split_at)),
                    url_decode(slice(pair, split_at + 1, len(pair))));
        }
    }
    return values;
}
