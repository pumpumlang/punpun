import std.data.checksum
import std.data.csv
import std.data.query
import std.data.ini
import std.data.binary

launch {
    say(crc32(bytes_from_text("hello")));
    say(adler32(bytes_from_text("hello")));
    say(fnv1a_text("a") != fnv1a_text("b"));

    let row = list<str>();
    list_push(row, "plain"); list_push(row, "has,comma"); list_push(row, "has\"quote");
    let line = csv_write_row(row);
    say(line);
    let back = csv_parse_row(line);
    say(list_size(back));
    say(list_at(back, 1));
    say(list_at(back, 2));

    say(url_encode("a b&c=d"));
    say(url_decode("a%20b%26c"));
    let q = query_decode("x=1&y=hello%20world");
    say(map_get(q, "y"));

    let cfg = ini_parse("[server]\nport = 8080\nname = demo\n; comment\n");
    say(ini_get(cfg, "server", "name"));
    say(ini_get_int(cfg, "server", "port", 0));

    let buf = bytes();
    write_u32_be(buf, 305419896);
    say(read_u32_be(buf, 0));
    let vb = bytes();
    write_varint(vb, 300);
    say(read_varint(vb, 0));
    say(varint_size(300));
}
