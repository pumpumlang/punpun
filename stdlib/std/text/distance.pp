// String similarity.

/// Levenshtein edit distance: the fewest single-character insertions,
/// deletions, or substitutions that turn one string into the other.
fn levenshtein(a: str, b: str) -> int {
    let a_length = len(a);
    let b_length = len(b);
    if a_length == 0 { return b_length; }
    if b_length == 0 { return a_length; }

    // Two rows instead of the full matrix: each row depends only on the one
    // before it, so the memory is O(min) rather than O(n*m).
    let mut previous = list<int>();
    for j in 0..b_length + 1 { list_push(previous, j); }

    for i in 0..a_length {
        let current = list<int>();
        list_push(current, i + 1);
        for j in 0..b_length {
            let mut cost = 1;
            if char_at(a, i) == char_at(b, j) { cost = 0; }
            let deletion = list_at(previous, j + 1) + 1;
            let insertion = list_at(current, j) + 1;
            let substitution = list_at(previous, j) + cost;
            let mut best = deletion;
            if insertion < best { best = insertion; }
            if substitution < best { best = substitution; }
            list_push(current, best);
        }
        previous = current;
    }
    return list_at(previous, b_length);
}

/// Similarity in 0..1, where 1 means identical.
fn similarity(a: str, b: str) -> float {
    let mut longest = len(a);
    if len(b) > longest { longest = len(b); }
    if longest == 0 { return 1.0; }
    return 1.0 - decimal(levenshtein(a, b)) / decimal(longest);
}

/// Hamming distance. Defined only for equal-length strings, because a
/// substitution-only distance has no meaning otherwise.
fn hamming(a: str, b: str) -> int {
    if len(a) != len(b) { panic("hamming distance needs equal-length strings"); }
    let mut total = 0;
    for i in 0..len(a) {
        if char_at(a, i) != char_at(b, i) { total = total + 1; }
    }
    return total;
}

fn common_prefix(a: str, b: str) -> str {
    let mut count = 0;
    let limit = min_len(a, b);
    while count < limit and char_at(a, count) == char_at(b, count) { count = count + 1; }
    return slice(a, 0, count);
}

fn min_len(a: str, b: str) -> int {
    if len(a) < len(b) { return len(a); }
    return len(b);
}

/// The closest entry in `options`, or "" when the list is empty.
fn closest_match(value: str, options: List<str>) -> str {
    if list_size(options) == 0 { return ""; }
    let mut best = list_at(options, 0);
    let mut best_distance = levenshtein(value, best);
    for i in 1..list_size(options) {
        let candidate = list_at(options, i);
        let distance = levenshtein(value, candidate);
        if distance < best_distance { best = candidate; best_distance = distance; }
    }
    return best;
}
