// Searching over List<int>.

/// Index of `value`, or -1. The list must be sorted ascending; on an unsorted
/// list the result is meaningless rather than merely slow.
fn binary_search(items: List<int>, value: int) -> int {
    let mut low = 0;
    let mut high = list_size(items) - 1;
    while low <= high {
        // Computed this way rather than (low + high) / 2, which can overflow
        // once the indices are large. PunPun traps on overflow, so that would
        // be a panic rather than a wrong answer, but a panic is still a bug.
        let middle = low + (high - low) / 2;
        let found = list_at(items, middle);
        if found == value { return middle; }
        if found < value { low = middle + 1; } else { high = middle - 1; }
    }
    return -1;
}

/// First index at which `value` could be inserted and keep the list sorted.
fn lower_bound(items: List<int>, value: int) -> int {
    let mut low = 0;
    let mut high = list_size(items);
    while low < high {
        let middle = low + (high - low) / 2;
        if list_at(items, middle) < value { low = middle + 1; } else { high = middle; }
    }
    return low;
}

fn linear_search(items: List<int>, value: int) -> int {
    for i in 0..list_size(items) {
        if list_at(items, i) == value { return i; }
    }
    return -1;
}

fn contains_int(items: List<int>, value: int) -> bool {
    return linear_search(items, value) >= 0;
}

fn count_int(items: List<int>, value: int) -> int {
    let mut total = 0;
    for i in 0..list_size(items) {
        if list_at(items, i) == value { total = total + 1; }
    }
    return total;
}

fn min_int_of(items: List<int>) -> int {
    if list_size(items) == 0 { panic("min of an empty list"); }
    let mut best = list_at(items, 0);
    for i in 1..list_size(items) {
        let value = list_at(items, i);
        if value < best { best = value; }
    }
    return best;
}

fn max_int_of(items: List<int>) -> int {
    if list_size(items) == 0 { panic("max of an empty list"); }
    let mut best = list_at(items, 0);
    for i in 1..list_size(items) {
        let value = list_at(items, i);
        if value > best { best = value; }
    }
    return best;
}

fn sum_ints(items: List<int>) -> int {
    let mut total = 0;
    for i in 0..list_size(items) { total = total + list_at(items, i); }
    return total;
}
