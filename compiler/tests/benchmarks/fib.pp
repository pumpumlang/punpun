// Call-heavy: dominated by function call overhead, prologue/epilogue cost, and
// stack traffic. This is the workload register allocation should move most.
fn fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
launch { say(fib(27)); }
