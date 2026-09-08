fn stats_sum(values: nums) -> i64 {
    let mut total = 0;
    for i in 0..size(values) {
        total = total + values[i];
    }
    return total;
}

fn stats_mean(values: nums) -> f64 {
    assert(size(values) > 0, "stats_mean requires a nonempty list");
    return decimal(stats_sum(values)) / decimal(size(values));
}

fn stats_min(values: nums) -> i64 {
    assert(size(values) > 0, "stats_min requires a nonempty list");
    let mut result = values[0];
    for i in 1..size(values) {
        if values[i] < result {
            result = values[i];
        }
    }
    return result;
}

fn stats_max(values: nums) -> i64 {
    assert(size(values) > 0, "stats_max requires a nonempty list");
    let mut result = values[0];
    for i in 1..size(values) {
        if values[i] > result {
            result = values[i];
        }
    }
    return result;
}

fn stats_sorted(values: nums) -> nums {
    let result = [];
    for i in 0..size(values) {
        push(result, values[i]);
    }
    sort(result);
    return result;
}
