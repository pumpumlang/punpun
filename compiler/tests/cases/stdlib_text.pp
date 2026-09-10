import std.text.strings
import std.text.format
import std.data.hex
import std.data.base64

launch {
    say(reverse("abc"));
    say(title_case("hello wide world"));
    say(count_occurrences("banana", "an"));
    say(list_size(split_whitespace("  a  b   c ")));
    say(center("x", 5, "-"));
    say(strip_prefix("prefix-body", "prefix-"));
    say(format_fixed(3.14159, 2));
    say(format_fixed(0.999, 2));
    say(format_bytes(1536));
    say(format_duration(90000));
    say(format_zero_padded(7, 3));
    say(hex_encode(bytes_from_text("Hi")));
    say(bytes_to_text(hex_decode("4869")));
    say(hex_of_int(255));
    say(base64_encode_text("Man"));
    say(base64_encode_text("Ma"));
    say(base64_decode_text(base64_encode_text("hello world")));
}
