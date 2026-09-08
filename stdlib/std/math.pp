fn minimum(left: i64, right: i64) -> i64 {
    if left < right {
        return left;
    } else {
        return right;
    }
}

fn maximum(left: i64, right: i64) -> i64 {
    if left > right {
        return left;
    } else {
        return right;
    }
}

fn clamp(value: i64, lower: i64, upper: i64) -> i64 {
    assert(lower <= upper, "clamp requires lower <= upper");
    return minimum(maximum(value, lower), upper);
}

fn gcd(left: i64, right: i64) -> i64 {
    // Work with negative magnitudes so the smallest int is a valid input.
    let mut a = left;
    let mut b = right;
    if a > 0 {
        a = -a;
    }
    if b > 0 {
        b = -b;
    }
    while b != 0 {
        // Avoid the overflowing smallest-int remainder by -1.
        if b == -1 {
            return 1;
        }
        let remainder = a % b;
        a = b;
        b = remainder;
    }
    return -a;
}

fn factorial(n: i64) -> i64 {
    assert(n >= 0 && n <= 20, "factorial requires 0 <= n <= 20");
    let mut result = 1;
    for factor in 2..n + 1 {
        result = result * factor;
    }
    return result;
}

fn integer_power(base: i64, exponent: i64) -> i64 {
    assert(exponent >= 0, "integer_power requires a nonnegative exponent");
    let mut result = 1;
    let mut factor = base;
    let mut remaining = exponent;
    while remaining > 0 {
        if remaining % 2 == 1 {
            result = result * factor;
        }
        remaining = remaining / 2;
        if remaining > 0 {
            factor = factor * factor;
        }
    }
    return result;
}
