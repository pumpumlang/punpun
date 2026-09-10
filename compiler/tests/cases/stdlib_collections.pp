import std.collections.sorting
import std.collections.search
import std.collections.set
import std.collections.counter
import std.collections.stack
import std.collections.queue

launch {
    let items = list<int>();
    random_seed(42);
    for i in 0..200 { list_push(items, random_int(0, 999)); }
    sort_ints(items);
    say(is_sorted_ints(items));
    say(binary_search(items, list_at(items, 77)) >= 0);
    say(min_int_of(items) <= max_int_of(items));

    let s = set_new();
    set_add(s, "a"); set_add(s, "b"); set_add(s, "a");
    say(set_size(s));

    let c = counter_new();
    counter_add(c, "x"); counter_add(c, "x"); counter_add(c, "y");
    say(counter_get(c, "x"));
    say(counter_most_common(c));
    say(counter_total(c));

    let st = list<int>();
    stack_push(st, 1); stack_push(st, 2);
    say(stack_pop(st));

    let q = list<int>();
    queue_push(q, 1); queue_push(q, 2);
    say(queue_pop(q));
}
