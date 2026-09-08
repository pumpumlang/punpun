fn expect(condition: bool, message: String) { assert(condition, message); }
fn expect_equal_i64(left: i64, right: i64, message: String) { assert(left == right, message); }
fn expect_equal_string(left: String, right: String, message: String) { assert(left == right, message); }
