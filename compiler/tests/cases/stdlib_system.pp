import std.system_ext.cli
import std.system_ext.console
import std.system_ext.log
import std.system_ext.timer
import std.system_ext.paths
import std.system_ext.files
import std.data.uuid
import std.math_ext.shuffling
import std.text.strings
import std.text.format

launch {
    let argv = list<str>();
    list_push(argv, "--name=demo"); list_push(argv, "--count=7");
    list_push(argv, "--verbose"); list_push(argv, "input.txt");
    let parsed = parse_arg_list(argv);
    say(get_option(parsed, "name", "?"));
    say(get_option_int(parsed, "count", 0));
    say(has_flag(parsed, "verbose"));
    say(positional_at(parsed, 0, "?"));

    say(len(green("ok")) > 2);
    say(progress_bar(0.5, 10));

    log_at(level_info(), level_warn(), "a warning");
    log_at(level_error(), level_info(), "suppressed");

    let w = stopwatch_start(stopwatch_new());
    say(stopwatch_elapsed(w) >= 0);
    say(format_elapsed(1500));

    say(without_extension("/a/b/file.tar"));
    say(with_extension("dir/file.txt", "md"));
    say(is_absolute("/x")); say(is_absolute("x"));
    say(list_size(split_path("/a/b/c")));

    let id = uuid4();
    say(len(id)); say(is_uuid(id)); say(is_uuid("nope"));

    random_seed(7);
    let nums2 = list<int>();
    for i in 0..10 { list_push(nums2, i); }
    shuffle_ints(nums2);
    say(list_size(nums2));
    say(list_size(sample_ints(nums2, 3)));
    say(random_bool(1.0));
}
