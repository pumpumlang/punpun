import std.data.json
import std.data.toml
import std.regex
import std.crypto.sha256
import std.compress.lzss
import std.compress.rle
import std.collections.functional
import std.collections.deque
import std.random
import std.datetime
import std.path
import std.filesystem
import std.config
import std.text.utf8
import std.data.mime
import std.process
import std.system_info
import std.testing
import std.archive.zip
import std.db.kv

fn triple_stdlib(value:int)->int{return value*3;}

launch {
    let parsed=json_parse("{\"name\":\"PunPun\",\"items\":[1,2,3],\"ok\":true}");
    assert(parsed.ok()&&parsed.value.get_string("name","")=="PunPun","json");
    assert(json_parse(json_stringify(parsed.value)).ok(),"json roundtrip");
    let toml=toml_parse("title=\"PunPun\"\n[build]\njobs=4\n");assert(toml.ok()&&toml.get_int("build.jobs",0)==4,"toml");
    assert(regex_is_match("^P.nP.n$","PunPun"),"regex");assert(regex_replace("\\d+","v123x","#")=="v#x","regex replace");
    assert(sha256_text_hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha256");
    assert(hmac_sha256_text_hex("key","The quick brown fox jumps over the lazy dog")=="f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8","hmac");
    let phrase="PunPun PunPun PunPun aaaaaaaaaaaaaaaaaaaa";let packed=lzss_compress_text(phrase);assert(lzss_decompress_text(packed)==phrase,"lzss");
    assert(bytes_to_text(rle_decompress(rle_compress(bytes_from_text("aaaabbbcc"))))=="aaaabbbcc","rle");
    let numbers=list<int>();for i in 1..5{list_push(numbers,i);}let factor=2;let mapped=list_map<int,int>(numbers,fn(x:int)->int{return x*factor;});assert(list_at(mapped,3)==8,"generic closure map");
    let q=Deque<int>();q.push_back(7);q.push_back(8);assert(q.pop_front()==7&&q.front()==8,"deque");
    let rng=Random(42);let first=rng.next_raw();assert(first!=rng.next_raw(),"rng");
    let date=DateTime(2026,9,11,18,30,5,123);assert(datetime_parse_iso(date.iso()).day==11,"datetime");
    assert(path_normalize("a/./b/../c")=="a/c"&&path_relative("a/b","a/c")=="../c","path");
    let config=config_from_toml("name=\"pp\"\ncount=3\n");assert(config.string("name","")=="pp"&&config.integer("count",0)==3,"config");
    let smile=utf8_encode_codepoint(128522);assert(utf8_at("x"+smile,1)==128522,"utf8");
    assert(mime_type("data.json")=="application/json","mime");
    let command=process_run("echo PunPun");assert(command.ok()&&trim(command.stdout)=="PunPun","process");
    assert(cpu_count()>=1&&len(hostname())>0,"system info");
    let archive_entries=list<ZipEntry>();list_push(archive_entries,ZipEntry("hello.txt",bytes_from_text("hello")));let archive=zip_read(zip_write(archive_entries));assert(archive.ok()&&bytes_to_text(archive.get("hello.txt"))=="hello","zip");
    let db_path="/tmp/punpun-stdlib-broad-kv.json";remove_file(db_path);let db=KeyValueDb(db_path);db.set_int("answer",42);assert(db.commit(),"db commit");let db_again=KeyValueDb(db_path);assert(db_again.get_int("answer",0)==42,"db reload");remove_file(db_path);
    let suite=TestSuite("stdlib");suite.check(true,"works");suite.assert_ok();
    say("stdlib-broad-ok");
}
