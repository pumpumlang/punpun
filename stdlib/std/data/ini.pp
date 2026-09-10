// INI configuration files.
//
// Keys are stored as "section.key" so one flat Map holds the whole file.
// Sectionless keys at the top are stored under their bare name.

fn ini_parse(source: str) -> Map<str> {
    let values = map<str>();
    let lines = split(replace(source, "\r\n", "\n"), "\n");
    let mut section = "";

    for i in 0..list_size(lines) {
        let line = trim(list_at(lines, i));
        if len(line) == 0 { continue; }

        let first = char_at(line, 0);
        // Both ';' and '#' start a comment; different tools emit each.
        if first == 59 or first == 35 { continue; }

        if first == 91 and ends_with(line, "]") {
            section = trim(slice(line, 1, len(line) - 1));
            continue;
        }

        let split_at = index_of(line, "=", 0);
        if split_at < 0 { continue; }
        let key = trim(slice(line, 0, split_at));
        let value = trim(slice(line, split_at + 1, len(line)));
        if len(section) > 0 {
            map_put(values, section + "." + key, value);
        } else {
            map_put(values, key, value);
        }
    }
    return values;
}

fn ini_get(values: Map<str>, section: str, key: str) -> str {
    if len(section) == 0 { return map_get_or(values, key, ""); }
    return map_get_or(values, section + "." + key, "");
}

fn ini_has(values: Map<str>, section: str, key: str) -> bool {
    if len(section) == 0 { return map_has(values, key); }
    return map_has(values, section + "." + key);
}

fn ini_get_int(values: Map<str>, section: str, key: str, fallback: int) -> int {
    let value_text = ini_get(values, section, key);
    if len(value_text) == 0 { return fallback; }
    return parse_int(value_text);
}

fn ini_get_bool(values: Map<str>, section: str, key: str, fallback: bool) -> bool {
    let value_text = to_lower(ini_get(values, section, key));
    if value_text == "true" or value_text == "yes" or value_text == "1" or value_text == "on" { return true; }
    if value_text == "false" or value_text == "no" or value_text == "0" or value_text == "off" { return false; }
    return fallback;
}
