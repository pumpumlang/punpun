import std.data.json
import std.data.toml
import std.regex
import std.crypto.sha256
import std.compress.lzss
import std.random
import std.datetime
import std.text.utf8
import std.archive.zip

launch {
    let broken=json_parse("{\"x\":]"); assert(!broken.ok()&&broken.offset>=0,"json error");
    let unicode=json_parse("\"\\uD83D\\uDE0A\""); assert(unicode.ok()&&utf8_at(unicode.value.string_value,0)==128522,"json surrogate");
    assert(!toml_parse("broken line").ok(),"toml error");
    assert(!regex_is_match("^a+$","bbb")&&list_size(regex_find_all("\\d+","a1b22c333"))==3,"regex edges");
    assert(sha256_text_hex("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha empty");
    let repetitive="abcabcabcabcabcabc";assert(lzss_decompress_text(lzss_compress_text(repetitive))==repetitive,"lzss edge");
    let a=Random(12345);let b=Random(12345);for i in 0..20{assert(a.next_raw()==b.next_raw(),"rng deterministic");}
    assert(datetime_valid(DateTime(2024,2,29,0,0,0,0))&&!datetime_valid(DateTime(2023,2,29,0,0,0,0)),"leap day");
    let entries=list<ZipEntry>();list_push(entries,ZipEntry("x",bytes_from_text("abc")));let encoded=zip_write(entries);let corrupt=bytes_slice(encoded,0,bytes_len(encoded));bytes_put(corrupt,31,bytes_at(corrupt,31)^1);assert(!zip_read(corrupt).ok(),"zip crc");
    say("stdlib-edges-ok");
}
