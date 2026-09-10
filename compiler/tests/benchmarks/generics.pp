// Monomorphization stress: many distinct specializations from few sources.
fn pick<T: Copy>(a: T, b: T, first: bool) -> T { if first { return a; } return b; }
struct Cell<T> { value: T, public fn get() -> T { return self.value; } }

fn total(rounds: int) -> int {
    let mut sum = 0;
    for i in 0..rounds {
        let a: Cell<int> = Cell(i);
        sum = sum + pick(a.get(), i, i % 2 == 0);
    }
    return sum;
}

launch {
    say(total(50000));
    say(pick("x", "y", true));
    say(pick(1.5, 2.5, false));
}
