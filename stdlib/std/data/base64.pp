// Base64, as defined by RFC 4648.

fn base64_alphabet() -> str {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

fn base64_encode(data: bytes) -> str {
    let alphabet = base64_alphabet();
    let length = bytes_len(data);
    let mut out = "";
    let mut i = 0;
    while i < length {
        // Three input bytes become four output characters. A short final group
        // is padded with '=' so the length is always a multiple of four.
        let b0 = bytes_at(data, i);
        let mut b1 = 0;
        let mut b2 = 0;
        let mut have = 1;
        if i + 1 < length { b1 = bytes_at(data, i + 1); have = 2; }
        if i + 2 < length { b2 = bytes_at(data, i + 2); have = 3; }

        let triple = b0 * 65536 + b1 * 256 + b2;
        let c0 = (triple / 262144) % 64;
        let c1 = (triple / 4096) % 64;
        let c2 = (triple / 64) % 64;
        let c3 = triple % 64;

        out = out + slice(alphabet, c0, c0 + 1) + slice(alphabet, c1, c1 + 1);
        if have >= 2 { out = out + slice(alphabet, c2, c2 + 1); } else { out = out + "="; }
        if have >= 3 { out = out + slice(alphabet, c3, c3 + 1); } else { out = out + "="; }
        i = i + 3;
    }
    return out;
}

fn base64_value(code: int) -> int {
    if code >= 65 and code <= 90 { return code - 65; }
    if code >= 97 and code <= 122 { return code - 71; }
    if code >= 48 and code <= 57 { return code + 4; }
    if code == 43 { return 62; }
    if code == 47 { return 63; }
    panic("invalid base64 character");
    return 0;
}

fn base64_decode(text_value: str) -> bytes {
    let length = len(text_value);
    if length % 4 != 0 { panic("base64 input length must be a multiple of 4"); }
    let out = bytes();
    let mut i = 0;
    while i < length {
        let c0 = base64_value(char_at(text_value, i));
        let c1 = base64_value(char_at(text_value, i + 1));
        let third = char_at(text_value, i + 2);
        let fourth = char_at(text_value, i + 3);

        let mut c2 = 0;
        let mut c3 = 0;
        let mut have = 1;
        if third != 61 { c2 = base64_value(third); have = 2; }
        if fourth != 61 { c3 = base64_value(fourth); have = 3; }

        let triple = c0 * 262144 + c1 * 4096 + c2 * 64 + c3;
        bytes_push(out, (triple / 65536) % 256);
        if have >= 2 { bytes_push(out, (triple / 256) % 256); }
        if have >= 3 { bytes_push(out, triple % 256); }
        i = i + 4;
    }
    return out;
}

fn base64_encode_text(text_value: str) -> str {
    return base64_encode(bytes_from_text(text_value));
}

fn base64_decode_text(text_value: str) -> str {
    return bytes_to_text(base64_decode(text_value));
}
