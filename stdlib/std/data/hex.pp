// Hexadecimal encoding.

fn hex_digits() -> str { return "0123456789abcdef"; }

fn hex_encode(data: bytes) -> str {
    let digits = hex_digits();
    let mut out = "";
    for i in 0..bytes_len(data) {
        let value = bytes_at(data, i);
        out = out + slice(digits, value / 16, value / 16 + 1);
        out = out + slice(digits, value % 16, value % 16 + 1);
    }
    return out;
}

fn hex_decode(text_value: str) -> bytes {
    let length = len(text_value);
    if length % 2 != 0 { panic("hex input must have an even number of digits"); }
    let out = bytes();
    let mut i = 0;
    while i < length {
        let high = hex_value(char_at(text_value, i));
        let low = hex_value(char_at(text_value, i + 1));
        bytes_push(out, high * 16 + low);
        i = i + 2;
    }
    return out;
}

fn hex_value(code: int) -> int {
    if code >= 48 and code <= 57 { return code - 48; }
    if code >= 97 and code <= 102 { return code - 87; }
    if code >= 65 and code <= 70 { return code - 55; }
    panic("invalid hex digit");
    return 0;
}

fn hex_of_int(value: int) -> str {
    if value == 0 { return "0"; }
    let digits = hex_digits();
    let mut remaining = value;
    let mut negative = false;
    if remaining < 0 { negative = true; remaining = 0 - remaining; }
    let mut out = "";
    while remaining > 0 {
        let digit = remaining % 16;
        out = slice(digits, digit, digit + 1) + out;
        remaining = remaining / 16;
    }
    if negative { return "-" + out; }
    return out;
}
