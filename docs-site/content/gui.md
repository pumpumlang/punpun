# GUI

The bundled `gui` package is an early native desktop foundation. It probes a native window system lazily and can display a basic native message/window path where a graphical session exists.

```punpun
bring gui;
launch {
    if gui_available() {
        gui_message("PunPun", "Hello from native GUI code");
    }
}
```

A browser-based GUI designer foundation is bundled under `gui-maker/`. The broad widget/layout system remains beta work.
