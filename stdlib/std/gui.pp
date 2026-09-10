# Cross-platform native GUI helpers.
#
# gui_available() and gui_message(title, message) are compiler builtins. On
# Windows they use Win32. Linux and other POSIX hosts use X11/XWayland when it
# is installed and a display is available; headless programs receive false.

fn gui_supported() -> bool {
    return gui_available();
}

fn gui_alert(message: String) -> bool {
    return gui_message("PunPun", message);
}
