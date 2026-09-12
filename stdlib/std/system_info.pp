# Small host-information layer over unavoidable OS queries.
struct SystemInfo { platform:str, hostname:str, cpu_count:int, current_dir:str }
fn system_info()->SystemInfo{return SystemInfo(platform(),hostname(),cpu_count(),current_dir());}
fn environment(name:str,fallback:str)->str{return env_or(name,fallback);}
fn environment_set(name:str,value:str)->bool{return env_set(name,value);}
fn arguments()->List<str>{let out=list<str>();for i in 0..arg_count(){list_push(out,arg(i));}return out;}
