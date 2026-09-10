// A binary min-heap over List<int>, usable as a priority queue.
//
// Stored as an implicit tree in the list: the children of index i live at
// 2i+1 and 2i+2. That needs no pointers and keeps the whole structure in one
// contiguous allocation.

fn heap_new() -> List<int> { return list<int>(); }

fn heap_push(heap: List<int>, value: int) {
    list_push(heap, value);
    // Sift the new element up until its parent is no larger.
    let mut index = list_size(heap) - 1;
    while index > 0 {
        let parent = (index - 1) / 2;
        if list_at(heap, parent) <= list_at(heap, index) { return; }
        let swap = list_at(heap, parent);
        list_put(heap, parent, list_at(heap, index));
        list_put(heap, index, swap);
        index = parent;
    }
}

fn heap_peek(heap: List<int>) -> int {
    if list_size(heap) == 0 { panic("peek at an empty heap"); }
    return list_at(heap, 0);
}

fn heap_pop(heap: List<int>) -> int {
    let count = list_size(heap);
    if count == 0 { panic("pop from an empty heap"); }
    let smallest = list_at(heap, 0);

    // Move the last element to the root, then sift it down.
    list_put(heap, 0, list_at(heap, count - 1));
    list_pop(heap);
    let size = list_size(heap);

    let mut index = 0;
    while true {
        let left = index * 2 + 1;
        let right = index * 2 + 2;
        let mut chosen = index;
        if left < size and list_at(heap, left) < list_at(heap, chosen) { chosen = left; }
        if right < size and list_at(heap, right) < list_at(heap, chosen) { chosen = right; }
        if chosen == index { break; }
        let swap = list_at(heap, chosen);
        list_put(heap, chosen, list_at(heap, index));
        list_put(heap, index, swap);
        index = chosen;
    }
    return smallest;
}

fn heap_size(heap: List<int>) -> int { return list_size(heap); }
fn heap_is_empty(heap: List<int>) -> bool { return list_size(heap) == 0; }

/// Sorts by draining the heap. O(n log n) and, unlike quicksort, has no
/// worst-case input that degrades it.
fn heap_sort(values: List<int>) -> List<int> {
    let heap = heap_new();
    for i in 0..list_size(values) { heap_push(heap, list_at(values, i)); }
    let out = list<int>();
    while !heap_is_empty(heap) { list_push(out, heap_pop(heap)); }
    return out;
}
