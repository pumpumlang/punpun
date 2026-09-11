// Sorting for List<int> and List<str>.
//
// Insertion sort below the cutoff, quicksort above it. Insertion sort wins on
// small inputs because its constant factor is tiny; quicksort wins once the
// n log n term dominates. The cutoff of 12 is the usual crossover point and is
// not tuned for any particular workload.
//
// The pivot is the median of three. A first-element pivot degrades to O(n^2) on
// already-sorted input, which is the single most common real-world shape.

fn sort_ints(items: List<int>) {
    quicksort_ints(items, 0, list_size(items) - 1);
}

fn quicksort_ints(items: List<int>, low: int, high: int) {
    if high - low < 12 {
        insertion_sort_ints(items, low, high);
        return;
    }
    let pivot = median_of_three(items, low, high);
    let mut left = low;
    let mut right = high;
    while left <= right {
        while list_at(items, left) < pivot { left = left + 1; }
        while list_at(items, right) > pivot { right = right - 1; }
        if left <= right {
            let swap = list_at(items, left);
            list_put(items, left, list_at(items, right));
            list_put(items, right, swap);
            left = left + 1;
            right = right - 1;
        }
    }
    if low < right { quicksort_ints(items, low, right); }
    if left < high { quicksort_ints(items, left, high); }
}

fn insertion_sort_ints(items: List<int>, low: int, high: int) {
    if high <= low { return; }
    for offset in 1..high - low + 1 {
        let index = low + offset;
        let value = list_at(items, index);
        let mut position = index - 1;
        while position >= low and list_at(items, position) > value {
            list_put(items, position + 1, list_at(items, position));
            position = position - 1;
        }
        list_put(items, position + 1, value);
    }
}

fn median_of_three(items: List<int>, low: int, high: int) -> int {
    let middle = low + (high - low) / 2;
    let a = list_at(items, low);
    let b = list_at(items, middle);
    let c = list_at(items, high);
    if a <= b and b <= c { return b; }
    if c <= b and b <= a { return b; }
    if b <= a and a <= c { return a; }
    if c <= a and a <= b { return a; }
    return c;
}

fn sort_strings(items: List<str>) {
    let count = list_size(items);
    // Strings compare through a runtime call, so the comparison dominates and
    // the algorithmic constant matters less. Insertion sort keeps this simple
    // and is stable, which matters more for text.
    for offset in 1..count {
        let value = list_at(items, offset);
        let mut position = offset - 1;
        while position >= 0 and list_at(items, position) > value {
            list_put(items, position + 1, list_at(items, position));
            position = position - 1;
        }
        list_put(items, position + 1, value);
    }
}

fn is_sorted_ints(items: List<int>) -> bool {
    for i in 1..list_size(items) {
        if list_at(items, i - 1) > list_at(items, i) { return false; }
    }
    return true;
}

fn reverse_ints(items: List<int>) {
    let count = list_size(items);
    for i in 0..count / 2 {
        let swap = list_at(items, i);
        list_put(items, i, list_at(items, count - 1 - i));
        list_put(items, count - 1 - i, swap);
    }
}

// Sort with a caller-supplied ordering.
//
// `before(a, b)` answers whether a comes first. Insertion sort is used rather
// than the quicksort above because the comparison is an indirect call: its cost
// dominates, and insertion sort performs fewer comparisons on the partially
// ordered inputs that a custom ordering is usually applied to. It is also
// stable, which a caller ordering by one field of several will expect.
fn sort_by(items: List<int>, before: fn(int, int) -> bool) {
    let count = list_size(items);
    let mut index = 1;
    while index < count {
        let value = list_at(items, index);
        let mut scan = index - 1;
        while scan >= 0 {
            if !before(value, list_at(items, scan)) { break; }
            list_put(items, scan + 1, list_at(items, scan));
            scan = scan - 1;
        }
        list_put(items, scan + 1, value);
        index = index + 1;
    }
}
