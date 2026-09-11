fn sieve_count(flags: nums, limit: i64) -> i64 {
    for i in 2..limit + 1 {
        flags[i] = 1;
    }

    let mut factor = 2;
    while factor <= limit / factor {
        if flags[factor] == 0 {
            factor = factor + 1;
            continue;
        }
        let mut multiple = factor * factor;
        while multiple <= limit {
            flags[multiple] = 0;
            multiple = multiple + factor;
        }
        factor = factor + 1;
    }

    let mut count = 0;
    for i in 2..limit + 1 {
        if flags[i] == 1 {
            count = count + 1;
        }
    }
    return count;
}

launch {
    assert(arg_count() <= 2, "usage: sieve [limit [rounds]]");
    let mut limit = 200000;
    let mut rounds = 5;
    if arg_count() >= 1 {
        limit = parse_int(arg(0));
    }
    if arg_count() == 2 {
        rounds = parse_int(arg(1));
    }
    assert(limit >= 2 && limit <= 5000000, "limit must be between 2 and 5000000");
    assert(rounds >= 1 && rounds <= 100, "rounds must be between 1 and 100");

    // Allocate once outside the timer; every round resets and reuses this list.
    let flags = numbers();
    for i in 0..limit + 1 {
        push(flags, 0);
    }
    let mut count = 0;
    let mut checksum = 0;
    let started = clock_ms();
    for round in 0..rounds {
        count = sieve_count(flags, limit);
        checksum = checksum + count;
    }
    let elapsed = clock_ms() - started;

    if limit == 200000 {
        assert(count == 17984, "unexpected prime count for the default limit");
    }
    say("Primes <= " + text(limit) + ": " + text(count));
    say("Rounds: " + text(rounds) + ", checksum: " + text(checksum));
    say("Sieve/reset/count time (ms): " + text(elapsed));
}
