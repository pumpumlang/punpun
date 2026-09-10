// Descriptive statistics over List<float>.

fn mean(values: List<float>) -> float {
    let count = list_size(values);
    if count == 0 { panic("mean of an empty list"); }
    let mut total = 0.0;
    for i in 0..count { total = total + list_at(values, i); }
    return total / decimal(count);
}

fn sum_floats(values: List<float>) -> float {
    let mut total = 0.0;
    for i in 0..list_size(values) { total = total + list_at(values, i); }
    return total;
}

fn min_float_of(values: List<float>) -> float {
    if list_size(values) == 0 { panic("min of an empty list"); }
    let mut best = list_at(values, 0);
    for i in 1..list_size(values) {
        let value = list_at(values, i);
        if value < best { best = value; }
    }
    return best;
}

fn max_float_of(values: List<float>) -> float {
    if list_size(values) == 0 { panic("max of an empty list"); }
    let mut best = list_at(values, 0);
    for i in 1..list_size(values) {
        let value = list_at(values, i);
        if value > best { best = value; }
    }
    return best;
}

/// Population variance, dividing by n. Use sample_variance for an estimate of a
/// wider population from a sample.
fn variance(values: List<float>) -> float {
    let count = list_size(values);
    if count == 0 { panic("variance of an empty list"); }
    let average = mean(values);
    let mut total = 0.0;
    for i in 0..count {
        let difference = list_at(values, i) - average;
        total = total + difference * difference;
    }
    return total / decimal(count);
}

/// Sample variance, dividing by n-1 (Bessel's correction). This is the right
/// choice when the data is a sample rather than the whole population.
fn sample_variance(values: List<float>) -> float {
    let count = list_size(values);
    if count < 2 { panic("sample variance needs at least two values"); }
    let average = mean(values);
    let mut total = 0.0;
    for i in 0..count {
        let difference = list_at(values, i) - average;
        total = total + difference * difference;
    }
    return total / decimal(count - 1);
}

fn standard_deviation(values: List<float>) -> float { return sqrt(variance(values)); }

fn median(values: List<float>) -> float {
    let count = list_size(values);
    if count == 0 { panic("median of an empty list"); }
    let sorted = sort_floats_copy(values);
    if count % 2 == 1 { return list_at(sorted, count / 2); }
    return (list_at(sorted, count / 2 - 1) + list_at(sorted, count / 2)) / 2.0;
}

/// Linear-interpolated percentile, matching the common "type 7" definition.
fn percentile(values: List<float>, fraction: float) -> float {
    let count = list_size(values);
    if count == 0 { panic("percentile of an empty list"); }
    if fraction < 0.0 or fraction > 1.0 { panic("percentile needs a fraction in 0..1"); }
    let sorted = sort_floats_copy(values);
    if count == 1 { return list_at(sorted, 0); }
    let position = fraction * decimal(count - 1);
    let lower = whole(floor(position));
    let upper = whole(ceil(position));
    if lower == upper { return list_at(sorted, lower); }
    let weight = position - decimal(lower);
    return list_at(sorted, lower) * (1.0 - weight) + list_at(sorted, upper) * weight;
}

fn range_of(values: List<float>) -> float {
    return max_float_of(values) - min_float_of(values);
}

/// Insertion sort into a fresh list, leaving the input untouched. Statistics
/// callers rarely want their data reordered as a side effect.
fn sort_floats_copy(values: List<float>) -> List<float> {
    let out = list<float>();
    for i in 0..list_size(values) { list_push(out, list_at(values, i)); }
    for offset in 1..list_size(out) {
        let value = list_at(out, offset);
        let mut position = offset - 1;
        while position >= 0 and list_at(out, position) > value {
            list_put(out, position + 1, list_at(out, position));
            position = position - 1;
        }
        list_put(out, position + 1, value);
    }
    return out;
}
