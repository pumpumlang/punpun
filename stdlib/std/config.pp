import std.data.json
import std.data.toml

# Layered application configuration. Values are typed JsonValue entries so a
# config can hold strings, numbers, booleans and arrays without stringly-typed
# conversions scattered across the application.
object Config {
    public let values:Map<JsonValue>;
    public init(){self.values=map<JsonValue>();}
    public fn has(key:str)->bool{return map_has(self.values,key);}
    public fn set(key:str,value:JsonValue)->void{map_put(self.values,key,value);}
    public fn get(key:str)->JsonValue{if !self.has(key){return json_null();}return map_get(self.values,key);}
    public fn string(key:str,fallback:str)->str{return self.get(key).string_or(fallback);}
    public fn integer(key:str,fallback:int)->int{return self.get(key).int_or(fallback);}
    public fn boolean(key:str,fallback:bool)->bool{return self.get(key).bool_or(fallback);}
    public fn overlay(other:Map<JsonValue>)->void{for key in map_keys(other){map_put(self.values,key,map_get(other,key));}}
    public fn env_string(key:str,env_name:str)->void{if env_has(env_name){self.set(key,json_string(env_or(env_name,"")));}}
    public fn env_int(key:str,env_name:str)->void{if env_has(env_name){self.set(key,json_int(parse_int(env_or(env_name,"0"))));}}
    public fn env_bool(key:str,env_name:str)->void{if env_has(env_name){let v=to_lower(env_or(env_name,""));self.set(key,json_bool(v=="1"||v=="true"||v=="yes"||v=="on"));}}
}
fn config_from_toml(source:str)->Config {let parsed=toml_parse(source);if !parsed.ok(){panic("TOML line "+text(parsed.line)+": "+parsed.error);}let config=Config();config.overlay(parsed.values);return config;}
fn config_load_toml(path_value:str)->Config{return config_from_toml(read_text(path_value));}
