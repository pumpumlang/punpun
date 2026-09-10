// A first-in, first-out queue over List<T>.
//
// Dequeue is O(n) because it shifts the remaining elements down. That is the
// honest trade for keeping the representation a plain List: a ring buffer would
// be O(1) but needs its own struct and index bookkeeping. For queues of a few
// thousand elements the shift is not measurable; past that, use two stacks.

fn queue_push<T: Copy>(items: List<T>, value: T) {
    list_push(items, value);
}

fn queue_pop<T: Copy>(items: List<T>) -> T {
    let count = list_size(items);
    if count == 0 { panic("pop from an empty queue"); }
    let front = list_at(items, 0);
    for i in 0..count - 1 {
        list_put(items, i, list_at(items, i + 1));
    }
    list_pop(items);
    return front;
}

fn queue_peek<T: Copy>(items: List<T>) -> T {
    if list_size(items) == 0 { panic("peek at an empty queue"); }
    return list_at(items, 0);
}

fn queue_is_empty<T: Copy>(items: List<T>) -> bool {
    return list_size(items) == 0;
}
