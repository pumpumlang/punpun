# GUI

PunPun 1.3 includes a small native GUI foundation.

```punpun
import std.gui

launch {
    if gui_supported() {
        gui_message("PunPun", "Hello from native GUI code");
    }
}
```

Windows uses User32 message boxes. Linux and other supported POSIX hosts load
X11 dynamically and work through XWayland where available. Headless sessions
return false from `gui_available()`; non-GUI programs continue normally.

A full widget, layout, event, and application lifecycle library is planned for
the 1.4 library release.
