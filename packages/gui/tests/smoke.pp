import src.main

launch {
    assert(gui_supported(), "headless GUI backend should be available in package tests");
    let window = GuiWindow("PunPun GUI smoke", 640, 480);
    assert(window.valid(), "window creation");

    let label = window.label("hello");
    let input = window.input("start");
    let check = window.checkbox("enabled");
    let slider = window.slider(10, 20, 15);
    let progress = window.progress(0, 100, 25);
    let canvas = window.canvas();

    gui_vbox(window.widgets, 12, 20, 300, 30, 5);
    assert(label.x() == 12 && label.y() == 20, "vbox geometry");
    assert(input.y() == 55, "vbox ordering");

    gui_hbox(window.widgets, 10, 50, 600, 28, 4);
    assert(label.x() == 10 && input.x() > label.x(), "hbox geometry");

    gui_grid(window.widgets, 3, 8, 12, 600, 32, 4);
    assert(label.y() == 12 && check.x() > input.x(), "grid first row");
    assert(slider.y() == 48, "grid second row");

    assert(input.set_text("changed") && input.text() == "changed", "input text");
    assert(check.set_checked(true) && check.checked(), "checkbox state");
    assert(slider.value() == 15, "slider state");
    progress.set_value(500);
    assert(progress.value() == 100, "range clamp");

    canvas.bounds(0, 250, 200, 100);
    assert(canvas.clear(16777215), "canvas clear");
    assert(canvas.rect(1, 2, 20, 10, 16711680, true), "canvas rectangle");
    assert(canvas.line(0, 0, 10, 10, 255), "canvas line");
    assert(canvas.draw_text(5, 15, "hi", 0), "canvas text");

    let wh = window.handle;
    let bh = window.button("close").handle;
    assert(window.post(gui_event_click(), bh, 0, "synthetic", 25, 25), "post event");
    let handler = fn(event: GuiEvent) -> void {
        if event.is_click() && event.widget == bh { gui_window_close(wh); }
    };
    gui_run(window, handler);
    assert(!gui_window_open(wh), "capturing event handler");
    say("gui-ok");
}
