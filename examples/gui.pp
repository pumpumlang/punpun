import std.gui

launch {
    if gui_supported() {
        gui_message("PunPun 1.4", "The native GUI library is working.");
    } else {
        say("No native GUI display is available.");
    }
}
