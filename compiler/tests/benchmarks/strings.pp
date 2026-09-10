// Allocation-heavy: every concat allocates through the runtime.
launch {
    let mut total = 0;
    for round in 0..3000 {
        let text = "item-" + text(round) + ":" + text(round * 2);
        total = total + len(text);
    }
    say(total);
}
