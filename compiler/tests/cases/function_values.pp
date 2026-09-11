// Functions as values: passed as arguments, stored in locals, and written
// inline. The value is the callee's index, so all three backends share one
// calling sequence.
fn double(x: int) -> int { return x * 2; }
fn negate(x: int) -> int { return 0 - x; }

fn apply(g: fn(int) -> int, v: int) -> int { return g(v); }

fn map_in_place(xs: nums, g: fn(int) -> int) {
    for i in 0..size(xs) { xs[i] = g(xs[i]); }
}

fn combine(a: int, b: int, g: fn(int, int) -> int) -> int { return g(a, b); }

launch {
    say(apply(double, 21));
    say(apply(negate, 5));

    // A local of function type, reassigned to a different function.
    let mut chosen: fn(int) -> int = double;
    say(chosen(50));
    chosen = negate;
    say(chosen(50));

    // A literal, both inline and bound.
    say(apply(fn(x: int) -> int { return x * 3; }, 14));
    let squared = fn(n: int) -> int { return n * n; };
    say(squared(9));

    // Two parameters, and a function value that drives a loop.
    say(combine(6, 7, fn(a: int, b: int) -> int { return a * b; }));
    let values = [1, 2, 3, 4];
    map_in_place(values, fn(n: int) -> int { return n + 10; });
    for v in values { print(v); print(" "); }
    say("");
}
