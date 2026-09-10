// Path manipulation, on top of the platform-aware builtins.

fn join_all(parts: List<str>) -> str {
    let mut out = "";
    for i in 0..list_size(parts) {
        let piece = list_at(parts, i);
        if len(piece) == 0 { continue; }
        if len(out) == 0 { out = piece; } else { out = path_join(out, piece); }
    }
    return out;
}

fn without_extension(path: str) -> str {
    let name = path_name(path);
    let dot = last_index_of(name, ".");
    // A leading dot marks a hidden file, not an extension, so it is kept.
    if dot <= 0 { return name; }
    return slice(name, 0, dot);
}

fn with_extension(path: str, extension: str) -> str {
    let parent = path_parent(path);
    let stem = without_extension(path);
    let name = stem + "." + extension;
    if parent == "." { return name; }
    return path_join(parent, name);
}

fn is_absolute(path: str) -> bool {
    if len(path) == 0 { return false; }
    if char_at(path, 0) == 47 or char_at(path, 0) == 92 { return true; }
    // A Windows path may begin with a drive letter and a colon.
    return len(path) > 2 and char_at(path, 1) == 58;
}

fn normalize_separators(path: str) -> str { return replace(path, "\\", "/"); }

fn split_path(path: str) -> List<str> {
    let out = list<str>();
    let parts = split(normalize_separators(path), "/");
    for i in 0..list_size(parts) {
        let piece = list_at(parts, i);
        if len(piece) > 0 { list_push(out, piece); }
    }
    return out;
}

fn has_extension(path: str, extension: str) -> bool {
    return equals_ignore_case(path_extension(path), extension);
}

/// Every regular file under `root`, recursively.
///
/// Recursion depth is bounded: a symlink loop would otherwise make this run
/// forever, and the runtime does not expose a way to detect one.
fn walk_files(root: str, max_depth: int) -> List<str> {
    let found = list<str>();
    walk_into(root, max_depth, found);
    return found;
}

fn walk_into(directory: str, depth_left: int, found: List<str>) {
    if depth_left < 0 { return; }
    let entries = list_dir(directory);
    for i in 0..list_size(entries) {
        let full = path_join(directory, list_at(entries, i));
        if is_dir(full) {
            walk_into(full, depth_left - 1, found);
        } else {
            list_push(found, full);
        }
    }
}
