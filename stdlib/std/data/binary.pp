// Fixed-width integer encoding, for file formats and wire protocols.
//
// Both byte orders are provided explicitly. A format specifies one, and picking
// the host's order silently would make the output non-portable.

fn write_u16_le(buffer: bytes, value: int) {
    if value < 0 or value > 65535 { panic("value does not fit in a u16"); }
    bytes_push(buffer, value & 255);
    bytes_push(buffer, (value >> 8) & 255);
}

fn write_u16_be(buffer: bytes, value: int) {
    if value < 0 or value > 65535 { panic("value does not fit in a u16"); }
    bytes_push(buffer, (value >> 8) & 255);
    bytes_push(buffer, value & 255);
}

fn write_u32_le(buffer: bytes, value: int) {
    if value < 0 or value > 4294967295 { panic("value does not fit in a u32"); }
    for i in 0..4 { bytes_push(buffer, (value >> (i * 8)) & 255); }
}

fn write_u32_be(buffer: bytes, value: int) {
    if value < 0 or value > 4294967295 { panic("value does not fit in a u32"); }
    for i in 0..4 { bytes_push(buffer, (value >> ((3 - i) * 8)) & 255); }
}

fn read_u16_le(buffer: bytes, offset: int) -> int {
    return bytes_at(buffer, offset) | (bytes_at(buffer, offset + 1) << 8);
}

fn read_u16_be(buffer: bytes, offset: int) -> int {
    return (bytes_at(buffer, offset) << 8) | bytes_at(buffer, offset + 1);
}

fn read_u32_le(buffer: bytes, offset: int) -> int {
    let mut value = 0;
    for i in 0..4 { value = value | (bytes_at(buffer, offset + i) << (i * 8)); }
    return value;
}

fn read_u32_be(buffer: bytes, offset: int) -> int {
    let mut value = 0;
    for i in 0..4 { value = (value << 8) | bytes_at(buffer, offset + i); }
    return value;
}

/// Variable-length integer, as used by Protocol Buffers and SQLite. Seven bits
/// per byte, with the high bit marking continuation.
fn write_varint(buffer: bytes, value: int) {
    if value < 0 { panic("varint encoding needs a nonnegative value"); }
    let mut remaining = value;
    while remaining >= 128 {
        bytes_push(buffer, (remaining & 127) | 128);
        remaining = remaining >> 7;
    }
    bytes_push(buffer, remaining);
}

fn read_varint(buffer: bytes, offset: int) -> int {
    let mut value = 0;
    let mut shift = 0;
    let mut position = offset;
    while position < bytes_len(buffer) {
        let byte_value = bytes_at(buffer, position);
        value = value | ((byte_value & 127) << shift);
        position = position + 1;
        if (byte_value & 128) == 0 { return value; }
        shift = shift + 7;
        if shift > 63 { panic("varint is too long"); }
    }
    panic("varint is truncated");
    return 0;
}

fn varint_size(value: int) -> int {
    if value < 0 { panic("varint encoding needs a nonnegative value"); }
    let mut count = 1;
    let mut remaining = value >> 7;
    while remaining > 0 { count = count + 1; remaining = remaining >> 7; }
    return count;
}
