fn fibonacci(n: i64) -> i64 {
    if n <= 1 {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

fn sum_to(limit: i64) -> i64 {
    let mut current = 1;
    let mut total: i64 = 0;
    while current <= limit {
        total = total + current;
        current = current + 1;
    }
    return total;
}
