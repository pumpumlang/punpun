import std.path
import std.data.hex

struct FileInfo { path:str, name:str, size:int, directory:bool }
fn file_info(value:str)->FileInfo{return FileInfo(value,path_name(value),file_size(value),is_dir(value));}
fn fs_copy_file(source:str,destination:str)->void {write_bytes(destination,read_bytes(source));}
fn fs_copy_tree(source:str,destination:str,max_depth:int)->void {
    if max_depth<0{panic("filesystem recursion depth exceeded");}
    if !is_dir(source){fs_copy_file(source,destination);return;}
    make_dirs(destination);for entry in list_dir(source){let source_child=path_join(source,entry);let destination_child=path_join(destination,entry);if is_dir(source_child){fs_copy_tree(source_child,destination_child,max_depth-1);}else{fs_copy_file(source_child,destination_child);}}
}
fn fs_remove_tree(target:str,max_depth:int)->bool {
    if max_depth<0{return false;} if !is_dir(target){return remove_file(target);}
    for entry in list_dir(target){let child=path_join(target,entry);if is_dir(child){if !fs_remove_tree(child,max_depth-1){return false;}}else if !remove_file(child){return false;}}
    return remove_dir(target);
}
fn fs_walk(root:str,max_depth:int)->List<FileInfo>{let out=list<FileInfo>();_fs_walk_into(root,max_depth,out);return out;}
fn _fs_walk_into(root:str,depth:int,out:List<FileInfo>)->void {if depth<0{return;}for entry in list_dir(root){let full=path_join(root,entry);list_push(out,file_info(full));if is_dir(full){_fs_walk_into(full,depth-1,out);}}}
fn fs_read_lines(path_value:str)->List<str>{return split(replace(read_text(path_value),"\r\n","\n"),"\n");}
fn fs_write_lines(path_value:str,lines:List<str>)->void{write_text(path_value,join(lines,"\n"));}
fn fs_temp_name(prefix:str,suffix:str)->str{return prefix+hex_encode(random_bytes(8))+suffix;}
fn fs_write_text_atomic(path_value:str,content:str)->void {let temp=path_value+"."+fs_temp_name("tmp-","");write_text(temp,content);if !rename_file(temp,path_value){remove_file(temp);panic("atomic rename failed");}}
