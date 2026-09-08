// Tiny assertion helpers for Punpun programs and library tests.

fn expect_int(actual: i64, expected: i64) -> void {
    assert(actual == expected, "expected int " + text(expected) + ", got " + text(actual));
}

fn expect_bool(actual: bool, expected: bool) -> void {
    assert(actual == expected, "boolean expectation failed");
}

fn expect_str(actual: String, expected: String) -> void {
    assert(actual == expected, "string expectation failed");
}
