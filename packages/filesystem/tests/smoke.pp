import src.main
launch {let name="pp-filesystem-smoke.tmp";file_write(name,"hello");assert(file_read(name)=="hello","read");assert(file_present(name),"exists");assert(normalize_path("a/./b/../c")=="a/c","normalize");remove_file(name);say("filesystem-ok");}
