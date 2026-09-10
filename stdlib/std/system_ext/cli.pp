// Command-line argument parsing.
//
// Supports `--name=value` for options, `--flag` for booleans, and everything
// else as positional. A bare `--` ends option parsing.
//
// `--name value` is deliberately NOT supported, because it is ambiguous without
// a schema: in `--verbose input.txt` there is no way to tell whether `verbose`
// is a boolean flag and `input.txt` a positional argument, or whether `verbose`
// takes `input.txt` as its value. Tools that accept that form resolve it with a
// declared option table; until this parser has one, requiring `=` makes every
// command line mean exactly one thing.
//
// Also absent: subcommands, short-option clustering, and required-argument
// validation. All need the same schema.

struct Args { flags: Map<bool>, options: Map<str>, positional: List<str> }

fn parse_args() -> Args {
    let raw_args = list<str>();
    for i in 0..arg_count() { list_push(raw_args, arg(i)); }
    return parse_arg_list(raw_args);
}

fn parse_arg_list(items: List<str>) -> Args {
    let flags = map<bool>();
    let options = map<str>();
    let positional = list<str>();

    let count = list_size(items);
    let mut i = 0;
    let mut only_positional = false;

    while i < count {
        let item = list_at(items, i);

        // A bare `--` ends option parsing, so a positional argument that looks
        // like an option can still be passed.
        if !only_positional and item == "--" {
            only_positional = true;
            i = i + 1;
            continue;
        }

        if !only_positional and starts_with(item, "--") and len(item) > 2 {
            let body = slice(item, 2, len(item));
            let equals = index_of(body, "=", 0);
            if equals >= 0 {
                map_put(options, slice(body, 0, equals), slice(body, equals + 1, len(body)));
            } else {
                map_put(flags, body, true);
            }
        } else {
            list_push(positional, item);
        }
        i = i + 1;
    }
    return Args(flags, options, positional);
}

fn has_flag(parsed: Args, name: str) -> bool {
    return map_get_or(parsed.flags, name, false);
}

fn get_option(parsed: Args, name: str, fallback: str) -> str {
    return map_get_or(parsed.options, name, fallback);
}

fn get_option_int(parsed: Args, name: str, fallback: int) -> int {
    let value = map_get_or(parsed.options, name, "");
    if len(value) == 0 { return fallback; }
    return parse_int(value);
}

fn positional_count(parsed: Args) -> int { return list_size(parsed.positional); }

fn positional_at(parsed: Args, index: int, fallback: str) -> str {
    if index < 0 or index >= list_size(parsed.positional) { return fallback; }
    return list_at(parsed.positional, index);
}
