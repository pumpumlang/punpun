import std.gui
launch {
    if gui_supported() {
        let window = GuiWindow("wide", 200, 100);
        let canvas = window.canvas();
        canvas.rect(1, 2, 30, 20, 16711935, true);
        window.close();
    }
}
