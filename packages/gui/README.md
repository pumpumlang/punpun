# gui

First-party PunPun 1.4 native GUI foundation.

```sh
ppx add gui
```

The package exposes `gui_available()`, `gui_message(title, message)`,
`gui_supported()`, and `gui_alert(message)`. Windows uses Win32. POSIX hosts
load X11/XWayland dynamically; unavailable or headless sessions return false.

This package currently provides a message-window foundation, not a complete
widget/layout toolkit.
