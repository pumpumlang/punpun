import std.gui

launch {
    if !gui_supported() {
        say("No GUI backend is available on this session.");
        return;
    }

    let window = GuiWindow("PunPun counter", 420, 220);
    let label = window.label("Clicks: 0");
    let button = window.button("Increment");
    let progress = window.progress(0, 10, 0);
    gui_vbox(window.widgets, 20, 20, 380, 36, 10);

    let window_handle = window.handle;
    let label_handle = label.handle;
    let button_handle = button.handle;
    let progress_handle = progress.handle;
    let mut count = 0;

    let handler = fn(event: GuiEvent) -> void {
        if event.is_click() && event.widget == button_handle {
            count = count + 1;
            gui_widget_set_text(label_handle, "Clicks: " + text(count));
            gui_widget_set_value(progress_handle, count);
            if count >= 10 { gui_window_close(window_handle); }
        }
        if event.is_close() { gui_window_close(window_handle); }
    };

    gui_run(window, handler);
}
