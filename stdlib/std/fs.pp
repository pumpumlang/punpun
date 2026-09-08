// Filesystem helpers built on the portable runtime primitives.

fn fs_exists(path: String) -> bool {
    return file_exists(path);
}

fn fs_read(path: String) -> String {
    return read_text(path);
}

fn fs_write(path: String, contents: String) -> void {
    write_text(path, contents);
}

fn fs_make_dir(path: String) -> bool { return make_dir(path); }
fn fs_remove(path: String) -> bool { return remove_file(path); }
fn fs_rename(source: String, destination: String) -> bool { return rename_file(source, destination); }
fn fs_join(left: String, right: String) -> String { return path_join(left, right); }
