// Random selection and shuffling.
//
// Every function here uses the seeded generator, so results are reproducible
// for a given seed. That is what a test or a simulation needs; anything
// security-sensitive must use random_bytes instead.

/// Fisher-Yates, in place.
///
/// The index must be chosen from the remaining range, not the whole list. The
/// naive version that picks from 0..n on every step produces a biased
/// permutation, and the bias is invisible without measuring the distribution.
fn shuffle_ints(items: List<int>) {
    let count = list_size(items);
    if count < 2 { return; }
    let mut i = count - 1;
    while i > 0 {
        let j = random_int(0, i);
        let swap = list_at(items, i);
        list_put(items, i, list_at(items, j));
        list_put(items, j, swap);
        i = i - 1;
    }
}

fn shuffle_strings(items: List<str>) {
    let count = list_size(items);
    if count < 2 { return; }
    let mut i = count - 1;
    while i > 0 {
        let j = random_int(0, i);
        let swap = list_at(items, i);
        list_put(items, i, list_at(items, j));
        list_put(items, j, swap);
        i = i - 1;
    }
}

fn choice_int(items: List<int>) -> int {
    let count = list_size(items);
    if count == 0 { panic("choice from an empty list"); }
    return list_at(items, random_int(0, count - 1));
}

fn choice_str(items: List<str>) -> str {
    let count = list_size(items);
    if count == 0 { panic("choice from an empty list"); }
    return list_at(items, random_int(0, count - 1));
}

/// `count` distinct elements, without replacement.
fn sample_ints(items: List<int>, count: int) -> List<int> {
    let total = list_size(items);
    if count > total { panic("cannot sample more elements than the list holds"); }
    let pool = list<int>();
    for i in 0..total { list_push(pool, list_at(items, i)); }
    shuffle_ints(pool);
    let out = list<int>();
    for i in 0..count { list_push(out, list_at(pool, i)); }
    return out;
}

fn random_range(low: float, high: float) -> float {
    return low + random_float() * (high - low);
}

fn random_bool(probability: float) -> bool {
    return random_float() < probability;
}

/// Normally distributed value, by the Box-Muller transform.
fn random_gaussian(mean_value: float, deviation: float) -> float {
    // u must be strictly positive, since log(0) is undefined.
    let mut u = random_float();
    while u <= 0.0 { u = random_float(); }
    let v = random_float();
    return mean_value + deviation * sqrt(0.0 - 2.0 * log(u)) * cos(6.2831853071795862 * v);
}

fn random_digits(count: int) -> str {
    let mut out = "";
    for i in 0..count { out = out + text(random_int(0, 9)); }
    return out;
}
