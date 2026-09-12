import std.testing
fn expect(condition: bool, message: String) { assert(condition, message); }
fn expect_equal_i64(left: i64, right: i64, message: String) { expect_int(left,right); }
fn expect_equal_string(left: String, right: String, message: String) { expect_str(left,right); }
