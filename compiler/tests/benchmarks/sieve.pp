// Memory- and loop-heavy: bounds checks, list indexing, tight inner loops.
launch {
    let limit = 300000;
    let flags = numbers();
    for i in 0..limit { push(flags, 0); }
    let mut count = 0;
    let mut n = 2;
    while n < limit {
        if flags[n] == 0 {
            count = count + 1;
            let mut m = n + n;
            while m < limit {
                put(flags, m, 1);
                m = m + n;
            }
        }
        n = n + 1;
    }
    say(count);
}
