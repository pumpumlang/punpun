# Platform-neutral lexical path manipulation in PunPun. No filesystem access is
# required for normalization/relative calculations.
struct Path {
    value:str,
    public fn name()->str{return path_name(self.value);}
    public fn parent()->Path{return Path(path_parent(self.value));}
    public fn extension()->str{return path_extension(self.value);}
    public fn join(child:str)->Path{return Path(path_join(self.value,child));}
    public fn normalized()->Path{return Path(path_normalize(self.value));}
    public fn absolute()->bool{return path_is_absolute(self.value);}
}
fn path(value:str)->Path{return Path(value);}
fn path_is_separator(c:int)->bool{return c==47||c==92;}
fn path_is_absolute(value:str)->bool {
    if len(value)==0{return false;} if path_is_separator(char_at(value,0)){return true;}
    return len(value)>=3&&char_at(value,1)==58&&path_is_separator(char_at(value,2));
}
fn path_normalize(value:str)->str {
    if len(value)==0{return ".";}
    let source=replace(value,"\\","/"); let rooted=starts_with(source,"/");
    let mut drive=""; let mut offset=0;
    if len(source)>=2&&char_at(source,1)==58{drive=slice(source,0,2);offset=2;if offset<len(source)&&char_at(source,offset)==47{offset=offset+1;}}
    let parts=split(slice(source,offset,len(source)),"/"); let stack=list<str>();
    for piece in parts {
        if len(piece)==0||piece=="."{continue;}
        if piece==".." {
            if list_size(stack)>0&&list_at(stack,list_size(stack)-1)!=".."{list_pop(stack);}
            else if !rooted&&len(drive)==0{list_push(stack,piece);}
        } else {list_push(stack,piece);}
    }
    let body=join(stack,"/");
    if len(drive)>0 {if len(body)>0{return drive+"/"+body;}return drive+"/";}
    if rooted {return "/"+body;} if len(body)==0{return ".";} return body;
}
fn path_parts(value:str)->List<str> {let normalized=path_normalize(value);let mut clean=normalized;if starts_with(clean,"/"){clean=slice(clean,1,len(clean));}return split(clean,"/");}
fn path_relative(from_path:str,to_path:str)->str {
    let left=path_parts(from_path);let right=path_parts(to_path);let mut common=0;
    while common<list_size(left)&&common<list_size(right)&&list_at(left,common)==list_at(right,common){common=common+1;}
    let out=list<str>();for i in common..list_size(left){list_push(out,"..");}for i in common..list_size(right){list_push(out,list_at(right,i));}
    if list_size(out)==0{return ".";}return join(out,"/");
}
fn path_change_extension(value:str,extension:str)->str {
    let parent=path_parent(value);let name=path_name(value);let dot=last_index_of(name,".");let mut stem=name;if dot>0{stem=slice(name,0,dot);}let mut ext=extension;if starts_with(ext,"."){ext=slice(ext,1,len(ext));}
    let mut result=stem;if len(ext)>0{result=result+"."+ext;}if parent=="."{return result;}return path_join(parent,result);
}
