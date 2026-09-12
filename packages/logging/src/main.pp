import std.logging

# Compatibility helpers plus access to the real Logger type in std.logging.
fn log_trace(message: String) { Logger("",log_trace_level(),"",false).trace(message); }
fn log_debug(message: String) { Logger("",log_debug_level(),"",false).debug(message); }
fn log_info(message: String)  { Logger("",log_info_level(),"",false).info(message); }
fn log_warn(message: String)  { Logger("",log_warn_level(),"",false).warn(message); }
fn log_error(message: String) { Logger("",log_error_level(),"",false).error(message); }
fn log_named(name:String,threshold:int)->Logger{return logger(name,threshold);}
fn log_file(name:String,threshold:int,path_value:String)->Logger{return file_logger(name,threshold,path_value);}
