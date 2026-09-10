// UUID version 4, per RFC 4122.

/// A random UUID, drawn from the operating system's entropy pool.
///
/// Uses random_bytes rather than the seeded generator: a UUID from a
/// predictable source is not unique in any sense that matters, and would be a
/// real vulnerability if used as a session or object identifier.
fn uuid4() -> str {
    let source = random_bytes(16);

    // Version 4 in the high nibble of byte 6, and variant 10xx in byte 8.
    // Without these the value is random but is not a valid UUID.
    bytes_put(source, 6, (bytes_at(source, 6) & 15) | 64);
    bytes_put(source, 8, (bytes_at(source, 8) & 63) | 128);

    let digits = "0123456789abcdef";
    let mut out = "";
    for i in 0..16 {
        if i == 4 or i == 6 or i == 8 or i == 10 { out = out + "-"; }
        let value = bytes_at(source, i);
        out = out + slice(digits, value / 16, value / 16 + 1);
        out = out + slice(digits, value % 16, value % 16 + 1);
    }
    return out;
}

fn is_uuid(value: str) -> bool {
    if len(value) != 36 { return false; }
    for i in 0..36 {
        let code = char_at(value, i);
        if i == 8 or i == 13 or i == 18 or i == 23 {
            if code != 45 { return false; }
        } else if !is_hex_digit(code) {
            return false;
        }
    }
    return true;
}

fn uuid_nil() -> str { return "00000000-0000-0000-0000-000000000000"; }
