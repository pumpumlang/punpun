import src.main

launch {
    let first = gui_available();
    let second = gui_supported();
    assert(first == second, "GUI wrappers must report the runtime state");
    say("gui-ok");
}
