import std.data.json
import std.regex
import std.crypto.sha256
import std.collections.functional
import std.archive.zip

fn square(value: int) -> int { return value * value; }

launch {
    let settings = json_parse("{\"name\":\"PunPun\",\"workers\":4}");
    assert(settings.ok(), "configuration should parse");
    say(settings.value.get_string("name", "unknown"));

    let versions = regex_find_all("\\d+", "compiler-1.5 build-42");
    say("numbers found: " + text(list_size(versions)));
    say("sha256: " + sha256_text_hex("PunPun"));

    let values = list<int>();
    for i in 1..5 { list_push(values, i); }
    let squares = list_map<int, int>(values, square);
    say("4 squared: " + text(list_at(squares, 3)));

    let files = list<ZipEntry>();
    list_push(files, ZipEntry("hello.txt", bytes_from_text("hello from PunPun\n")));
    let archive = zip_read(zip_write(files));
    assert(archive.ok(), "ZIP should round-trip");
    say("zip entry: " + bytes_to_text(archive.get("hello.txt")));
}
