import std.data.json

# PPX json now delegates to the full PunPun standard-library implementation.
# The old 0.1 convenience functions remain source-compatible.
fn json_valid(text: String) -> bool { return json_parse(text).ok(); }
fn json_get_string(text: String, key: String, fallback: String) -> String {
    let parsed=json_parse(text);if !parsed.ok(){return fallback;}return parsed.value.get_string(key,fallback);
}
fn json_get_i64(text: String, key: String, fallback: i64) -> i64 {
    let parsed=json_parse(text);if !parsed.ok(){return fallback;}return parsed.value.get_int(key,fallback);
}
fn json_quote(text: String) -> String { return json_stringify(json_string(text)); }
fn json_decode(text:String)->JsonParseResult{return json_parse(text);}
fn json_encode(value:JsonValue)->String{return json_stringify(value);}
fn json_encode_pretty(value:JsonValue,indent:int)->String{return json_pretty(value,indent);}
