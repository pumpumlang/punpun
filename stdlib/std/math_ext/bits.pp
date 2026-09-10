// Bit manipulation on int.
//
// int is a signed 64-bit value, so bit 63 is the sign bit. Shifts are checked:
// a shift amount outside 0..63 traps rather than being undefined.

// Note on precedence: `==` binds tighter than `&`, as in C. Every bit test here
// parenthesizes the mask explicitly, because `value & 1 == 1` would otherwise
// mean `value & (1 == 1)` and fail to type-check rather than misbehave quietly.
fn bit_get(value: int, index: int) -> bool {
    if index < 0 or index > 63 { panic("bit index must be in 0..63"); }
    return ((value >> index) & 1) == 1;
}

fn bit_set(value: int, index: int) -> int {
    if index < 0 or index > 63 { panic("bit index must be in 0..63"); }
    return value | (1 << index);
}

fn bit_clear(value: int, index: int) -> int {
    if index < 0 or index > 63 { panic("bit index must be in 0..63"); }
    return value & ~(1 << index);
}

fn bit_toggle(value: int, index: int) -> int {
    if index < 0 or index > 63 { panic("bit index must be in 0..63"); }
    return value ^ (1 << index);
}

fn popcount(value: int) -> int {
    let mut count = 0;
    for i in 0..64 {
        if ((value >> i) & 1) == 1 { count = count + 1; }
    }
    return count;
}

fn leading_zeros(value: int) -> int {
    if value == 0 { return 64; }
    let mut count = 0;
    let mut index = 63;
    while index >= 0 {
        if ((value >> index) & 1) == 1 { return count; }
        count = count + 1;
        index = index - 1;
    }
    return count;
}

fn trailing_zeros(value: int) -> int {
    if value == 0 { return 64; }
    let mut count = 0;
    for i in 0..64 {
        if ((value >> i) & 1) == 1 { return count; }
        count = count + 1;
    }
    return count;
}

fn is_power_of_two(value: int) -> bool {
    return value > 0 and (value & (value - 1)) == 0;
}

fn next_power_of_two(value: int) -> int {
    if value <= 1 { return 1; }
    let mut result = 1;
    while result < value { result = result << 1; }
    return result;
}

fn to_binary(value: int) -> str {
    if value == 0 { return "0"; }
    let mut out = "";
    let mut remaining = value;
    let mut negative = false;
    if remaining < 0 { negative = true; remaining = 0 - remaining; }
    while remaining > 0 {
        if remaining % 2 == 1 { out = "1" + out; } else { out = "0" + out; }
        remaining = remaining / 2;
    }
    if negative { return "-" + out; }
    return out;
}

fn from_binary(digits: str) -> int {
    let mut result = 0;
    for i in 0..len(digits) {
        let code = char_at(digits, i);
        if code == 48 { result = result * 2; }
        else if code == 49 { result = result * 2 + 1; }
        else { panic("invalid binary digit"); }
    }
    return result;
}

/// Rotate left within 64 bits. Shifts are on the unsigned bit pattern, so the
/// sign bit rotates like any other.
fn rotate_left(value: int, amount: int) -> int {
    let places = ((amount % 64) + 64) % 64;
    if places == 0 { return value; }
    return (value << places) | ((value >> (64 - places)) & ((1 << places) - 1));
}
