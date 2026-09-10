// A set of strings, backed by Map.
//
// Only str elements: Map keys are always str, and a general set would need a
// hash and equality for arbitrary T, which PunPun cannot express without
// first-class functions. For other element types, convert to a key first.

fn set_new() -> Map<bool> {
    return map<bool>();
}

fn set_add(items: Map<bool>, value: str) {
    map_put(items, value, true);
}

fn set_has(items: Map<bool>, value: str) -> bool {
    return map_has(items, value);
}

fn set_remove(items: Map<bool>, value: str) -> bool {
    return map_remove(items, value);
}

fn set_size(items: Map<bool>) -> int {
    return map_size(items);
}

fn set_values(items: Map<bool>) -> List<str> {
    return map_keys(items);
}

fn set_union(left: Map<bool>, right: Map<bool>) -> Map<bool> {
    let out = set_new();
    let a = map_keys(left);
    for i in 0..list_size(a) { set_add(out, list_at(a, i)); }
    let b = map_keys(right);
    for i in 0..list_size(b) { set_add(out, list_at(b, i)); }
    return out;
}

fn set_intersection(left: Map<bool>, right: Map<bool>) -> Map<bool> {
    let out = set_new();
    let a = map_keys(left);
    for i in 0..list_size(a) {
        let key = list_at(a, i);
        if map_has(right, key) { set_add(out, key); }
    }
    return out;
}

fn set_difference(left: Map<bool>, right: Map<bool>) -> Map<bool> {
    let out = set_new();
    let a = map_keys(left);
    for i in 0..list_size(a) {
        let key = list_at(a, i);
        if !map_has(right, key) { set_add(out, key); }
    }
    return out;
}
