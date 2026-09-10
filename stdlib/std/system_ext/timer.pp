// Timing and simple benchmarking.

struct Stopwatch { started: int, accumulated: int, running: bool }

fn stopwatch_new() -> Stopwatch { return Stopwatch(0, 0, false); }

fn stopwatch_start(watch: Stopwatch) -> Stopwatch {
    if watch.running { return watch; }
    return Stopwatch(clock_ms(), watch.accumulated, true);
}

fn stopwatch_stop(watch: Stopwatch) -> Stopwatch {
    if !watch.running { return watch; }
    return Stopwatch(0, watch.accumulated + clock_ms() - watch.started, false);
}

/// Elapsed milliseconds, including the currently running interval.
fn stopwatch_elapsed(watch: Stopwatch) -> int {
    if watch.running { return watch.accumulated + clock_ms() - watch.started; }
    return watch.accumulated;
}

fn stopwatch_reset() -> Stopwatch { return stopwatch_new(); }

/// Median of `rounds` timings of a fixed workload size. The median rather than
/// the mean, because one scheduling hiccup skews a mean badly and a median not
/// at all.
fn time_rounds(rounds: int) -> List<int> {
    let samples = list<int>();
    for i in 0..rounds { list_push(samples, clock_ms()); }
    return samples;
}

fn elapsed_since(start: int) -> int { return clock_ms() - start; }

fn format_elapsed(milliseconds: int) -> str {
    if milliseconds < 1000 { return text(milliseconds) + "ms"; }
    return text(milliseconds / 1000) + "." + pad_left(text(milliseconds % 1000), 3, "0") + "s";
}
