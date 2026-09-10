// The smallest complete PunPun program, in the modern dialect.
fn double(value: int) -> int {
    return value * 2;
}

launch {
    say("hello from ppc");
    say(double(21));
}
