# Date/time value types and ISO-8601 formatting/parsing in PunPun.
# Host primitives only provide wall/monotonic clocks and local decomposition.

struct Duration {
    milliseconds: int,
    public fn seconds() -> float { return decimal(self.milliseconds) / 1000.0; }
    public fn minutes() -> float { return self.seconds() / 60.0; }
    public fn hours() -> float { return self.minutes() / 60.0; }
}
fn milliseconds(value:int)->Duration{return Duration(value);}
fn seconds(value:int)->Duration{return Duration(value*1000);}
fn minutes(value:int)->Duration{return Duration(value*60000);}
fn hours(value:int)->Duration{return Duration(value*3600000);}

struct DateTime {
    year:int, month:int, day:int, hour:int, minute:int, second:int, millisecond:int,
    public fn iso() -> str { return datetime_format_iso(self); }
}

fn datetime_now_local() -> DateTime {
    let stamp=now_ms();
    return DateTime(year_of(stamp),month_of(stamp),day_of(stamp),hour_of(stamp),minute_of(stamp),second_of(stamp),stamp%1000);
}
fn datetime_is_leap_year(year:int)->bool{return (year%4==0 && year%100!=0)||year%400==0;}
fn datetime_days_in_month(year:int,month:int)->int {
    if month==2 {if datetime_is_leap_year(year){return 29;} return 28;}
    if month==4||month==6||month==9||month==11{return 30;} return 31;
}
fn datetime_valid(value:DateTime)->bool {
    if value.month<1||value.month>12||value.day<1||value.day>datetime_days_in_month(value.year,value.month){return false;}
    return value.hour>=0&&value.hour<24&&value.minute>=0&&value.minute<60&&value.second>=0&&value.second<60&&value.millisecond>=0&&value.millisecond<1000;
}
fn _days_from_civil(year:int,month:int,day:int)->int {
    let mut y=year; if month<=2{y=y-1;}
    let mut era=0; if y>=0{era=y/400;}else{era=(y-399)/400;}
    let yoe=y-era*400; let mut mp=month+9; if month>2{mp=month-3;}
    let doy=(153*mp+2)/5+day-1; let doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+doe-719468;
}
fn datetime_to_epoch_ms_utc(value:DateTime)->int {
    if !datetime_valid(value){panic("invalid DateTime");}
    return _days_from_civil(value.year,value.month,value.day)*86400000+value.hour*3600000+value.minute*60000+value.second*1000+value.millisecond;
}
fn _dt_two(v:int)->str{return pad_left(text(v),2,"0");}
fn _dt_three(v:int)->str{return pad_left(text(v),3,"0");}
fn datetime_format_iso(value:DateTime)->str {
    return pad_left(text(value.year),4,"0")+"-"+_dt_two(value.month)+"-"+_dt_two(value.day)+"T"+_dt_two(value.hour)+":"+_dt_two(value.minute)+":"+_dt_two(value.second)+"."+_dt_three(value.millisecond);
}
fn _dt_digits(source:str,start:int,count:int)->int {
    if start<0||start+count>len(source){return -1;} let mut value=0;
    for i in 0..count{let c=char_at(source,start+i);if c<48||c>57{return -1;}value=value*10+(c-48);}return value;
}
fn datetime_parse_iso(source:str)->DateTime {
    let text_value=trim(source); if len(text_value)<19{return DateTime(0,0,0,0,0,0,0);}
    let year=_dt_digits(text_value,0,4);let month=_dt_digits(text_value,5,2);let day=_dt_digits(text_value,8,2);
    let hour=_dt_digits(text_value,11,2);let minute=_dt_digits(text_value,14,2);let second=_dt_digits(text_value,17,2);
    if char_at(text_value,4)!=45||char_at(text_value,7)!=45||(char_at(text_value,10)!=84&&char_at(text_value,10)!=32)||char_at(text_value,13)!=58||char_at(text_value,16)!=58{return DateTime(0,0,0,0,0,0,0);}
    let mut ms=0; if len(text_value)>=23&&char_at(text_value,19)==46{ms=_dt_digits(text_value,20,3);}
    let result=DateTime(year,month,day,hour,minute,second,ms); if !datetime_valid(result){return DateTime(0,0,0,0,0,0,0);} return result;
}
fn duration_between_ms(start:int,finish:int)->Duration{return Duration(finish-start);}
