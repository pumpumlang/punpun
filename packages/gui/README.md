# gui

First-party PunPun retained native GUI toolkit.

```sh
ppx add gui
```

`std.gui` now provides application windows, labels, buttons, text inputs,
checkboxes, sliders, progress bars, panels and canvases. Widgets have retained
text/value/visibility/enabled/bounds state, backend-independent vertical,
horizontal and grid layouts, event polling/posting, and closure-driven event
loops. Canvas controls support RGB clear, rectangle, line and text drawing.

Windows uses Win32 controls and GDI. POSIX hosts load X11/XWayland dynamically,
so the compiler and SDK retain no X11 build dependency. Set
`PUNPUN_GUI_HEADLESS=1` to use the same retained model without a display for CI
or deterministic application tests.
