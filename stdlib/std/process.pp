# Process helpers. Runtime primitive only executes/captures a shell command;
# quoting, result modelling, argument assembly and async wrappers live here.
struct ProcessResult {
    command:str,
    status:int,
    stdout:str,
    public fn ok()->bool{return self.status==0;}
    public fn lines()->List<str>{return split(replace(self.stdout,"\r\n","\n"),"\n");}
}
fn shell_quote_posix(value:str)->str {
    if len(value)==0{return "''";}
    # Close quote, emit an escaped quote, reopen. Shell folklore, now mercifully
    # hidden in one function instead of every package inventing its own version.
    return "'"+replace(value,"'","'\\''")+"'";
}
fn shell_quote_windows(value:str)->str {
    # cmd.exe quoting is spectacularly historical. This handles ordinary args
    # and escapes embedded double quotes; callers needing shell metacharacters
    # should pass an explicit command string instead.
    return "\""+replace(value,"\"","\\\"")+"\"";
}
fn shell_quote(value:str)->str {if platform()=="windows"{return shell_quote_windows(value);}return shell_quote_posix(value);}
fn process_command(program:str,args:List<str>)->str {let mut out=shell_quote(program);for value in args{out=out+" "+shell_quote(value);}return out;}
fn process_run(command:str)->ProcessResult {let output=process_capture(command);return ProcessResult(command,process_status(),output);}
fn process_run_args(program:str,args:List<str>)->ProcessResult {return process_run(process_command(program,args));}
async fn process_capture_async(command:str)->str{return process_capture(command);}
async fn process_status_async(command:str)->int{process_capture(command);return process_status();}
