// Operations over List<T>.
//
// PunPun has no closures, so the shapes that would normally take a predicate —
// map, filter, reduce — cannot be written generically. What is here instead are
// the structural operations that need no callback, plus concrete int and str
// versions of the ones that do.

fn list_copy<T: Copy>(items: List<T>) -> List<T> {
    let out = list<T>();
    for i in 0..list_size(items) { list_push(out, list_at(items, i)); }
    return out;
}

fn list_concat<T: Copy>(left: List<T>, right: List<T>) -> List<T> {
    let out = list<T>();
    for i in 0..list_size(left) { list_push(out, list_at(left, i)); }
    for i in 0..list_size(right) { list_push(out, list_at(right, i)); }
    return out;
}

fn list_reverse<T: Copy>(items: List<T>) {
    let count = list_size(items);
    for i in 0..count / 2 {
        let swap = list_at(items, i);
        list_put(items, i, list_at(items, count - 1 - i));
        list_put(items, count - 1 - i, swap);
    }
}

fn list_slice<T: Copy>(items: List<T>, start: int, end: int) -> List<T> {
    let count = list_size(items);
    let mut low = start;
    let mut high = end;
    if low < 0 { low = 0; }
    if high > count { high = count; }
    let out = list<T>();
    let mut i = low;
    while i < high { list_push(out, list_at(items, i)); i = i + 1; }
    return out;
}

fn list_swap<T: Copy>(items: List<T>, a: int, b: int) {
    let held = list_at(items, a);
    list_put(items, a, list_at(items, b));
    list_put(items, b, held);
}

fn list_fill<T: Copy>(count: int, value: T) -> List<T> {
    let out = list<T>();
    for i in 0..count { list_push(out, value); }
    return out;
}

/// Removes the element at `index`, shifting the rest down. O(n).
fn list_remove_at<T: Copy>(items: List<T>, index: int) {
    let count = list_size(items);
    if index < 0 or index >= count { panic("remove index is out of range"); }
    let mut i = index;
    while i < count - 1 {
        list_put(items, i, list_at(items, i + 1));
        i = i + 1;
    }
    list_pop(items);
}

fn list_insert_at<T: Copy>(items: List<T>, index: int, value: T) {
    let count = list_size(items);
    if index < 0 or index > count { panic("insert index is out of range"); }
    list_push(items, value);
    let mut i = count;
    while i > index {
        list_put(items, i, list_at(items, i - 1));
        i = i - 1;
    }
    list_put(items, index, value);
}

/// Splits into fixed-size chunks. The final chunk may be shorter.
fn chunk_ints(items: List<int>, size: int) -> List<int> {
    // Returns a flat list of chunk boundaries rather than a list of lists,
    // because List<List<int>> would need a recursive type, which PunPun does
    // not yet support. Each pair is (start, length).
    if size <= 0 { panic("chunk size must be positive"); }
    let bounds = list<int>();
    let count = list_size(items);
    let mut start = 0;
    while start < count {
        let mut length = size;
        if start + length > count { length = count - start; }
        list_push(bounds, start);
        list_push(bounds, length);
        start = start + size;
    }
    return bounds;
}

fn dedupe_ints(items: List<int>) -> List<int> {
    let out = list<int>();
    let seen = map<bool>();
    for i in 0..list_size(items) {
        let value = list_at(items, i);
        let key = text(value);
        if !map_has(seen, key) {
            map_put(seen, key, true);
            list_push(out, value);
        }
    }
    return out;
}

fn dedupe_strings(items: List<str>) -> List<str> {
    let out = list<str>();
    let seen = map<bool>();
    for i in 0..list_size(items) {
        let value = list_at(items, i);
        if !map_has(seen, value) {
            map_put(seen, value, true);
            list_push(out, value);
        }
    }
    return out;
}

fn filter_greater(items: List<int>, threshold: int) -> List<int> {
    let out = list<int>();
    for i in 0..list_size(items) {
        let value = list_at(items, i);
        if value > threshold { list_push(out, value); }
    }
    return out;
}

fn map_scale(items: List<int>, factor: int) -> List<int> {
    let out = list<int>();
    for i in 0..list_size(items) { list_push(out, list_at(items, i) * factor); }
    return out;
}

fn list_index_of_str(items: List<str>, value: str) -> int {
    for i in 0..list_size(items) {
        if list_at(items, i) == value { return i; }
    }
    return -1;
}

fn list_contains_str(items: List<str>, value: str) -> bool {
    return list_index_of_str(items, value) >= 0;
}
