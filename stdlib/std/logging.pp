import std.datetime

fn log_trace_level()->int{return 5;}fn log_debug_level()->int{return 10;}fn log_info_level()->int{return 20;}fn log_warn_level()->int{return 30;}fn log_error_level()->int{return 40;}
fn log_level_name(level:int)->str {if level<=5{return "TRACE";}if level<=10{return "DEBUG";}if level<=20{return "INFO";}if level<=30{return "WARN";}return "ERROR";}
struct LogRecord { timestamp:int, level:int, logger:str, message:str }
fn log_record_format(record:LogRecord,timestamps:bool)->str {
    let mut prefix="";if timestamps{prefix=datetime_now_local().iso()+" ";}if len(record.logger)>0{prefix=prefix+"["+record.logger+"] ";}
    return prefix+pad_right(log_level_name(record.level),5," ")+" "+record.message;
}
object Logger {
    public let name:str;public let threshold:int;public let file_path:str;public let timestamps:bool;
    public init(name:str,threshold:int,file_path:str,timestamps:bool){self.name=name;self.threshold=threshold;self.file_path=file_path;self.timestamps=timestamps;}
    public fn enabled(level:int)->bool{return level>=self.threshold;}
    public fn log(level:int,message:str)->void {if !self.enabled(level){return;}let record=LogRecord(now_ms(),level,self.name,message);let line=log_record_format(record,self.timestamps);say(line);if len(self.file_path)>0{append_text(self.file_path,line+"\n");}}
    public fn trace(message:str)->void{self.log(log_trace_level(),message);}public fn debug(message:str)->void{self.log(log_debug_level(),message);}public fn info(message:str)->void{self.log(log_info_level(),message);}public fn warn(message:str)->void{self.log(log_warn_level(),message);}public fn error(message:str)->void{self.log(log_error_level(),message);}
}
fn logger(name:str,threshold:int)->Logger{return Logger(name,threshold,"",true);}
fn file_logger(name:str,threshold:int,path_value:str)->Logger{return Logger(name,threshold,path_value,true);}
