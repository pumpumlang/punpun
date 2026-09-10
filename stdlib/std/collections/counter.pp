// Frequency counting over string keys.

fn counter_new() -> Map<int> {
    return map<int>();
}

fn counter_add(counts: Map<int>, key: str) {
    map_put(counts, key, map_get_or(counts, key, 0) + 1);
}

fn counter_add_many(counts: Map<int>, key: str, amount: int) {
    map_put(counts, key, map_get_or(counts, key, 0) + amount);
}

fn counter_get(counts: Map<int>, key: str) -> int {
    return map_get_or(counts, key, 0);
}

fn counter_total(counts: Map<int>) -> int {
    let keys = map_keys(counts);
    let mut total = 0;
    for i in 0..list_size(keys) { total = total + map_get(counts, list_at(keys, i)); }
    return total;
}

/// The key with the highest count. Ties resolve to whichever key the map
/// happens to yield first, which is not specified — pass a sorted key list if
/// determinism matters.
fn counter_most_common(counts: Map<int>) -> str {
    let keys = map_keys(counts);
    if list_size(keys) == 0 { return ""; }
    let mut best = list_at(keys, 0);
    let mut best_count = map_get(counts, best);
    for i in 1..list_size(keys) {
        let key = list_at(keys, i);
        let count = map_get(counts, key);
        if count > best_count { best = key; best_count = count; }
    }
    return best;
}
