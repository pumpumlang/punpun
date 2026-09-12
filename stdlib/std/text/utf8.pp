# UTF-8 encode/decode utilities implemented in PunPun over byte strings.
fn utf8_encode_codepoint(codepoint:int)->str {
    if codepoint<0||codepoint>1114111||(codepoint>=55296&&codepoint<=57343){panic("invalid Unicode codepoint");}
    if codepoint<=127{return char_str(codepoint);}
    if codepoint<=2047{return char_str(192|(codepoint>>6))+char_str(128|(codepoint&63));}
    if codepoint<=65535{return char_str(224|(codepoint>>12))+char_str(128|((codepoint>>6)&63))+char_str(128|(codepoint&63));}
    return char_str(240|(codepoint>>18))+char_str(128|((codepoint>>12)&63))+char_str(128|((codepoint>>6)&63))+char_str(128|(codepoint&63));
}
fn _utf8_cont(c:int)->bool{return c>=128&&c<=191;}
fn utf8_decode(value:str)->List<int> {
    let out=list<int>();let mut i=0;
    while i<len(value){
        let a=char_at(value,i);
        if a<=127{list_push(out,a);i=i+1;continue;}
        if a>=194&&a<=223{
            if i+1>=len(value)||!_utf8_cont(char_at(value,i+1)){panic("invalid UTF-8");}
            list_push(out,((a&31)<<6)|(char_at(value,i+1)&63));i=i+2;continue;
        }
        if a>=224&&a<=239{
            if i+2>=len(value)||!_utf8_cont(char_at(value,i+1))||!_utf8_cont(char_at(value,i+2)){panic("invalid UTF-8");}
            let cp=((a&15)<<12)|((char_at(value,i+1)&63)<<6)|(char_at(value,i+2)&63);
            if cp<2048||(cp>=55296&&cp<=57343){panic("invalid UTF-8");}list_push(out,cp);i=i+3;continue;
        }
        if a>=240&&a<=244{
            if i+3>=len(value)||!_utf8_cont(char_at(value,i+1))||!_utf8_cont(char_at(value,i+2))||!_utf8_cont(char_at(value,i+3)){panic("invalid UTF-8");}
            let cp=((a&7)<<18)|((char_at(value,i+1)&63)<<12)|((char_at(value,i+2)&63)<<6)|(char_at(value,i+3)&63);
            if cp<65536||cp>1114111{panic("invalid UTF-8");}list_push(out,cp);i=i+4;continue;
        }
        panic("invalid UTF-8");
    }
    return out;
}
fn utf8_encode(codepoints:List<int>)->str {let mut out="";for cp in codepoints{out=out+utf8_encode_codepoint(cp);}return out;}
fn utf8_reverse(value:str)->str {let cps=utf8_decode(value);let mut out="";let mut i=list_size(cps)-1;while i>=0{out=out+utf8_encode_codepoint(list_at(cps,i));i=i-1;}return out;}
fn utf8_at(value:str,index:int)->int {let cps=utf8_decode(value);if index<0||index>=list_size(cps){panic("UTF-8 index out of range");}return list_at(cps,index);}
fn utf8_slice(value:str,start:int,end:int)->str {let cps=utf8_decode(value);let mut low=start;let mut high=end;if low<0{low=0;}if high>list_size(cps){high=list_size(cps);}let mut out="";for i in low..high{out=out+utf8_encode_codepoint(list_at(cps,i));}return out;}
