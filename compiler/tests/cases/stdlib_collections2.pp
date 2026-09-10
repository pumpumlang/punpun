import std.collections.list_ops
import std.collections.heap
import std.collections.grid
import std.text.casing
import std.text.distance
import std.text.wrap
import std.text.strings

launch {
    let a = list<int>();
    for i in 0..5 { list_push(a, i); }
    list_reverse(a); say(list_at(a, 0));
    say(list_size(list_slice(a, 1, 3)));
    list_remove_at(a, 0); say(list_size(a));
    list_insert_at(a, 0, 42); say(list_at(a, 0));
    let d = list<int>();
    list_push(d, 1); list_push(d, 1); list_push(d, 2);
    say(list_size(dedupe_ints(d)));

    let h = heap_new();
    heap_push(h, 5); heap_push(h, 1); heap_push(h, 3);
    say(heap_pop(h)); say(heap_pop(h)); say(heap_size(h));
    let raw_vals = list<int>();
    list_push(raw_vals, 9); list_push(raw_vals, 2); list_push(raw_vals, 7);
    say(list_at(heap_sort(raw_vals), 0));

    let g = grid_new(3, 3, 0);
    grid_set(g, 1, 1, 5);
    say(grid_get(g, 1, 1));
    say(grid_neighbour_sum(g, 0, 0));
    say(grid_count(g, 0));

    say(to_snake_case("HelloWorld"));
    say(to_camel_case("hello_wide_world"));
    say(to_pascal_case("hello world"));
    say(to_kebab_case("SomeName"));

    say(levenshtein("kitten", "sitting"));
    say(hamming("abc", "abd"));
    say(common_prefix("prefix", "preach"));

    say(list_size(wrap_text("the quick brown fox jumps", 10)));
    say(truncate_text("abcdefgh", 5, "..."));
}
