// Line-oriented and structured file helpers.

fn read_lines(path: str) -> List<str> {
    return split_lines(read_text(path));
}

fn write_lines(path: str, lines: List<str>) {
    write_text(path, join(lines, "\n") + "\n");
}

fn append_line(path: str, line: str) {
    append_text(path, line + "\n");
}

fn count_lines(path: str) -> int { return list_size(read_lines(path)); }

fn read_lines_non_empty(path: str) -> List<str> {
    let out = list<str>();
    let lines = read_lines(path);
    for i in 0..list_size(lines) {
        let line = list_at(lines, i);
        if len(trim(line)) > 0 { list_push(out, line); }
    }
    return out;
}

fn copy_file(source: str, destination: str) {
    // Through bytes, not text: a text round-trip would corrupt binary content
    // and reject any file containing a NUL.
    write_bytes(destination, read_bytes(source));
}

fn ensure_parent_dir(path: str) -> bool {
    let parent = path_parent(path);
    if parent == "." { return true; }
    return make_dirs(parent);
}

/// Writes by way of a temporary file and a rename, so a reader never observes a
/// half-written file and a crash mid-write leaves the original intact.
fn write_text_atomic(path: str, content: str) {
    let staging = path + ".tmp";
    write_text(staging, content);
    if !rename_file(staging, path) {
        remove_file(staging);
        panic("could not replace '" + path + "'");
    }
}

fn read_text_or(path: str, fallback: str) -> str {
    if !file_exists(path) { return fallback; }
    return read_text(path);
}
