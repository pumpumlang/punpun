// A C function returning `bool` sets only %al; the System V ABI leaves the rest
// of %rax unspecified. The native backend stored all 64 bits, so negating a
// false boolean returned from a builtin tested garbage and produced the wrong
// branch. This is exactly the shape that broke: `if !map_has(...)`.
launch {
    let m = map<bool>();
    map_put(m, "x", true);
    let present = map_has(m, "x");
    let absent = map_has(m, "y");
    say(present); say(absent);
    say(!present); say(!absent);
    if !absent { say("absent-branch-taken"); }
    if !present { say("WRONG"); } else { say("present-branch-taken"); }
}
