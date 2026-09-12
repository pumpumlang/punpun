import std.filesystem
import std.path

fn file_read(path_value: String) -> String { return read_text(path_value); }
fn file_write(path_value: String, contents: String) { write_text(path_value, contents); }
fn file_present(path_value: String) -> bool { return file_exists(path_value); }
fn working_directory() -> String { return current_dir(); }
fn file_copy(source:String,destination:String)->void{fs_copy_file(source,destination);}
fn tree_copy(source:String,destination:String)->void{fs_copy_tree(source,destination,64);}
fn tree_remove(target:String)->bool{return fs_remove_tree(target,64);}
fn normalize_path(value:String)->String{return path_normalize(value);}
