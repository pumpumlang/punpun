// A last-in, first-out stack over List<T>.
//
// Kept as free functions rather than methods because a generic method would
// have to live on a generic struct, and wrapping List<T> in one buys nothing:
// the list already is the storage.

fn stack_push<T: Copy>(items: List<T>, value: T) {
    list_push(items, value);
}

fn stack_pop<T: Copy>(items: List<T>) -> T {
    if list_size(items) == 0 { panic("pop from an empty stack"); }
    return list_pop(items);
}

fn stack_peek<T: Copy>(items: List<T>) -> T {
    if list_size(items) == 0 { panic("peek at an empty stack"); }
    return list_at(items, list_size(items) - 1);
}

fn stack_is_empty<T: Copy>(items: List<T>) -> bool {
    return list_size(items) == 0;
}
