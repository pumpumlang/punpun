// Integer mathematics.
//
// Everything here respects PunPun's checked arithmetic: an operation that would
// overflow traps rather than wrapping, so a function that could overflow says
// so in its documentation instead of quietly producing nonsense.

fn gcd(a: int, b: int) -> int {
    let mut left = abs(a);
    let mut right = abs(b);
    while right != 0 {
        let remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

/// Least common multiple. Divides before multiplying so the intermediate stays
/// as small as possible; multiplying first would trap on inputs whose product
/// exceeds int range even when the result does not.
fn lcm(a: int, b: int) -> int {
    if a == 0 or b == 0 { return 0; }
    return abs(a / gcd(a, b) * b);
}

fn is_even(value: int) -> bool { return value % 2 == 0; }
fn is_odd(value: int) -> bool { return value % 2 != 0; }

fn sign_of(value: int) -> int {
    if value > 0 { return 1; }
    if value < 0 { return 0 - 1; }
    return 0;
}

fn min_of(a: int, b: int) -> int { if a < b { return a; } return b; }
fn max_of(a: int, b: int) -> int { if a > b { return a; } return b; }

fn clamp(value: int, low: int, high: int) -> int {
    if low > high { panic("clamp needs low <= high"); }
    if value < low { return low; }
    if value > high { return high; }
    return value;
}

fn is_prime(value: int) -> bool {
    if value < 2 { return false; }
    if value < 4 { return true; }
    if value % 2 == 0 { return false; }
    // Trial division by odd numbers only, stopping at the square root: a
    // composite always has a factor at or below it.
    let mut divisor = 3;
    while divisor * divisor <= value {
        if value % divisor == 0 { return false; }
        divisor = divisor + 2;
    }
    return true;
}

fn next_prime(value: int) -> int {
    let mut candidate = value + 1;
    if candidate < 2 { candidate = 2; }
    while !is_prime(candidate) { candidate = candidate + 1; }
    return candidate;
}

fn primes_up_to(limit: int) -> List<int> {
    let found = list<int>();
    if limit < 2 { return found; }
    // Sieve of Eratosthenes: marking multiples is far cheaper than testing each
    // candidate for primality.
    let composite = list<bool>();
    for i in 0..limit + 1 { list_push(composite, false); }
    let mut value = 2;
    while value <= limit {
        if !list_at(composite, value) {
            list_push(found, value);
            let mut multiple = value * value;
            while multiple <= limit {
                list_put(composite, multiple, true);
                multiple = multiple + value;
            }
        }
        value = value + 1;
    }
    return found;
}

fn factorial(value: int) -> int {
    if value < 0 { panic("factorial of a negative number"); }
    // Traps on overflow past 20!, which is the largest that fits in an int.
    let mut total = 1;
    for i in 0..value { total = total * (i + 1); }
    return total;
}

/// (base ^ exponent) mod modulus, by repeated squaring.
fn pow_mod(base: int, exponent: int, modulus: int) -> int {
    if modulus <= 0 { panic("pow_mod needs a positive modulus"); }
    if exponent < 0 { panic("pow_mod needs a nonnegative exponent"); }
    let mut result = 1 % modulus;
    let mut factor = base % modulus;
    let mut remaining = exponent;
    while remaining > 0 {
        if remaining % 2 == 1 { result = result * factor % modulus; }
        factor = factor * factor % modulus;
        remaining = remaining / 2;
    }
    return result;
}

fn int_pow(base: int, exponent: int) -> int {
    if exponent < 0 { panic("int_pow needs a nonnegative exponent"); }
    let mut result = 1;
    let mut factor = base;
    let mut remaining = exponent;
    while remaining > 0 {
        if remaining % 2 == 1 { result = result * factor; }
        remaining = remaining / 2;
        if remaining > 0 { factor = factor * factor; }
    }
    return result;
}

fn int_sqrt(value: int) -> int {
    if value < 0 { panic("int_sqrt of a negative number"); }
    if value < 2 { return value; }
    // Newton's method on integers; converges in a handful of steps and avoids
    // the rounding a float sqrt would introduce near perfect squares.
    let mut guess = value;
    let mut next_guess = (guess + 1) / 2;
    while next_guess < guess {
        guess = next_guess;
        next_guess = (guess + value / guess) / 2;
    }
    return guess;
}

fn digits_of(value: int) -> List<int> {
    let out = list<int>();
    let mut remaining = abs(value);
    if remaining == 0 { list_push(out, 0); return out; }
    while remaining > 0 {
        list_push(out, remaining % 10);
        remaining = remaining / 10;
    }
    // Collected least-significant first, so reverse into reading order.
    let ordered = list<int>();
    for i in 0..list_size(out) {
        list_push(ordered, list_at(out, list_size(out) - 1 - i));
    }
    return ordered;
}

fn digit_sum(value: int) -> int {
    let mut total = 0;
    let mut remaining = abs(value);
    while remaining > 0 { total = total + remaining % 10; remaining = remaining / 10; }
    return total;
}

fn fibonacci(index: int) -> int {
    if index < 0 { panic("fibonacci needs a nonnegative index"); }
    let mut previous = 0;
    let mut current = 1;
    for i in 0..index {
        let sum = previous + current;
        previous = current;
        current = sum;
    }
    return previous;
}
