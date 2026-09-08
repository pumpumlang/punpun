fn file_read(path: String) -> String { return read_text(path); }
fn file_write(path: String, contents: String) { write_text(path, contents); }
fn file_present(path: String) -> bool { return file_exists(path); }
fn working_directory() -> String { return current_dir(); }
