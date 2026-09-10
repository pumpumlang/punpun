fn gcd(a: int, b: int) -> int { if b == 0 { return a; } return gcd(b, a % b); }
fn ackermann(m: int, n: int) -> int {
    if m == 0 { return n + 1; }
    if n == 0 { return ackermann(m - 1, 1); }
    return ackermann(m - 1, ackermann(m, n - 1));
}
launch { say(gcd(1071, 462)); say(ackermann(2, 3)); }
