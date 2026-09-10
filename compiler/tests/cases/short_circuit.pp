fn loud(tag: str, value: bool) -> bool { say(tag); return value; }
launch {
    say(loud("a", false) and loud("b", true));
    say(loud("c", true) or loud("d", false));
    say(loud("e", true) and loud("f", false));
}
