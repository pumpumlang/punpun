// Branch- and tag-dispatch heavy.
enum Op { Add(int), Mul(int), Neg, Stop }

fn apply(value: int, op: Op) -> int {
    return match op {
        Op::Add(n) => value + n,
        Op::Mul(n) => value * n,
        Op::Neg => 0 - value,
        Op::Stop => value,
    };
}

launch {
    let mut acc = 1;
    for i in 0..200000 {
        let step = i % 4;
        if step == 0 { acc = apply(acc, Op::Add(3)); }
        else if step == 1 { acc = apply(acc, Op::Mul(1)); }
        else if step == 2 { acc = apply(acc, Op::Neg); }
        else { acc = apply(acc, Op::Stop); }
        acc = acc % 100000;
    }
    say(acc);
}
