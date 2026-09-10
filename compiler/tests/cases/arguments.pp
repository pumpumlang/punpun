fn greet(name: str, greeting: str = "hello", excited: bool = false) -> str {
    if excited { return greeting + ", " + name + "!"; }
    return greeting + ", " + name;
}
launch {
    say(greet("ada"));
    say(greet("bob", "hi"));
    say(greet("cy", excited: true));
    say(greet(greeting: "yo", name: "dee"));
}
