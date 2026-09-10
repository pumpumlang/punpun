launch {
    let greeting = "hello" + " " + "world";
    say(greeting);
    say(len(greeting));
    say(slice(greeting, 0, 5));
    say(contains(greeting, "lo w"));
    say(text(-42));
    say(parse_int("-1234") + 1);
    say("abc" == "abc"); say("abc" == "abd"); say("abc" < "abd");
    say(utf8_len("héllo"));
    say(utf8_valid("héllo"));
}
