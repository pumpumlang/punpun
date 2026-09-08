contract Comparable<T> {
    fn same(other: T) -> bool;
}

struct Pair<T: Copy, U> {
    first: T;
    second: Result<U, Option<T>>;
}

fn identity<T: Copy + Comparable<T>>(value: T) -> T {
    return value;
}

launch {}
