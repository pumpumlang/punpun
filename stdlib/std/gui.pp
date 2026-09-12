# Retained cross-platform GUI toolkit.
#
# Native windows use Win32 on Windows and X11/XWayland on POSIX. Set
# PUNPUN_GUI_HEADLESS=1 for deterministic CI/model tests without a display.
# Every control is represented by a small runtime handle; layout and event-loop
# helpers live here in PunPun so all compiler backends observe identical rules.

fn gui_supported() -> bool { return gui_available(); }
fn gui_is_headless() -> bool { return gui_headless(); }
fn gui_alert(message: str) -> bool { return gui_message("PunPun", message); }

# Widget kinds.
fn gui_label_kind() -> int { return 1; }
fn gui_button_kind() -> int { return 2; }
fn gui_input_kind() -> int { return 3; }
fn gui_checkbox_kind() -> int { return 4; }
fn gui_slider_kind() -> int { return 5; }
fn gui_progress_kind() -> int { return 6; }
fn gui_panel_kind() -> int { return 7; }
fn gui_canvas_kind() -> int { return 8; }

# Event kinds.
fn gui_event_none() -> int { return 0; }
fn gui_event_close() -> int { return 1; }
fn gui_event_click() -> int { return 2; }
fn gui_event_change() -> int { return 3; }
fn gui_event_key() -> int { return 4; }
fn gui_event_text_changed() -> int { return 5; }
fn gui_event_mouse_move() -> int { return 6; }
fn gui_event_mouse_down() -> int { return 7; }
fn gui_event_mouse_up() -> int { return 8; }
fn gui_event_resize() -> int { return 9; }
fn gui_event_paint() -> int { return 10; }

object GuiEvent {
    public let kind: int;
    public let window: int;
    public let widget: int;
    public let key: int;
    public let text: str;
    public let x: int;
    public let y: int;

    public init(kind: int, window: int, widget: int, key: int, text: str, x: int, y: int) {
        self.kind = kind;
        self.window = window;
        self.widget = widget;
        self.key = key;
        self.text = text;
        self.x = x;
        self.y = y;
    }

    public fn is_close() -> bool { return self.kind == gui_event_close(); }
    public fn is_click() -> bool { return self.kind == gui_event_click(); }
    public fn is_change() -> bool { return self.kind == gui_event_change(); }
    public fn is_text() -> bool { return self.kind == gui_event_text_changed(); }
}

object GuiWidget {
    public let handle: int;
    public let kind: int;

    public init(handle: int, kind: int) {
        self.handle = handle;
        self.kind = kind;
    }

    public fn valid() -> bool { return self.handle > 0; }
    public fn bounds(x: int, y: int, width: int, height: int) -> bool {
        return gui_widget_set_bounds(self.handle, x, y, width, height);
    }
    public fn x() -> int { return gui_widget_x(self.handle); }
    public fn y() -> int { return gui_widget_y(self.handle); }
    public fn width() -> int { return gui_widget_width(self.handle); }
    public fn height() -> int { return gui_widget_height(self.handle); }
    public fn set_text(value: str) -> bool { return gui_widget_set_text(self.handle, value); }
    public fn text() -> str { return gui_widget_text(self.handle); }
    public fn set_value(value: int) -> bool { return gui_widget_set_value(self.handle, value); }
    public fn value() -> int { return gui_widget_value(self.handle); }
    public fn set_range(minimum: int, maximum: int) -> bool {
        return gui_widget_set_range(self.handle, minimum, maximum);
    }
    public fn visible(value: bool) -> bool { return gui_widget_set_visible(self.handle, value); }
    public fn enabled(value: bool) -> bool { return gui_widget_set_enabled(self.handle, value); }
    public fn checked() -> bool { return self.value() != 0; }
    public fn set_checked(value: bool) -> bool {
        if value { return self.set_value(1); }
        return self.set_value(0);
    }
    public fn destroy() -> bool { return gui_widget_destroy(self.handle); }

    # Canvas drawing. These return false on non-canvas controls.
    public fn clear(rgb: int) -> bool { return gui_canvas_clear(self.handle, rgb); }
    public fn rect(x: int, y: int, width: int, height: int, rgb: int, filled: bool) -> bool {
        return gui_canvas_rect(self.handle, x, y, width, height, rgb, filled);
    }
    public fn line(x1: int, y1: int, x2: int, y2: int, rgb: int) -> bool {
        return gui_canvas_line(self.handle, x1, y1, x2, y2, rgb);
    }
    public fn draw_text(x: int, y: int, value: str, rgb: int) -> bool {
        return gui_canvas_text(self.handle, x, y, value, rgb);
    }
}

object GuiWindow {
    public let handle: int;
    public let widgets: List<GuiWidget>;

    public init(title: str, width: int, height: int) {
        self.handle = gui_window_create(title, width, height);
        self.widgets = list<GuiWidget>();
    }

    public fn valid() -> bool { return self.handle > 0; }
    public fn open() -> bool { return gui_window_open(self.handle); }
    public fn width() -> int { return gui_window_width(self.handle); }
    public fn height() -> int { return gui_window_height(self.handle); }
    public fn title(value: str) -> bool { return gui_window_set_title(self.handle, value); }
    public fn show() -> bool { return gui_window_show(self.handle, true); }
    public fn hide() -> bool { return gui_window_show(self.handle, false); }
    public fn close() -> bool { return gui_window_close(self.handle); }
    public fn redraw() -> bool { return gui_redraw(self.handle); }

    public fn add(kind: int, text: str) -> GuiWidget {
        let widget = GuiWidget(gui_widget_create(self.handle, kind, text), kind);
        list_push(self.widgets, widget);
        return widget;
    }
    public fn label(text: str) -> GuiWidget { return self.add(gui_label_kind(), text); }
    public fn button(text: str) -> GuiWidget { return self.add(gui_button_kind(), text); }
    public fn input(text: str) -> GuiWidget { return self.add(gui_input_kind(), text); }
    public fn checkbox(text: str) -> GuiWidget { return self.add(gui_checkbox_kind(), text); }
    public fn slider(minimum: int, maximum: int, value: int) -> GuiWidget {
        let widget = self.add(gui_slider_kind(), "");
        widget.set_range(minimum, maximum);
        widget.set_value(value);
        return widget;
    }
    public fn progress(minimum: int, maximum: int, value: int) -> GuiWidget {
        let widget = self.add(gui_progress_kind(), "");
        widget.set_range(minimum, maximum);
        widget.set_value(value);
        return widget;
    }
    public fn panel() -> GuiWidget { return self.add(gui_panel_kind(), ""); }
    public fn canvas() -> GuiWidget { return self.add(gui_canvas_kind(), ""); }

    public fn poll(timeout_ms: int) -> GuiEvent {
        let kind = gui_poll(self.handle, timeout_ms);
        return GuiEvent(kind, gui_event_window(), gui_event_widget(), gui_event_key(),
                        gui_event_text(), gui_event_x(), gui_event_y());
    }

    public fn post(kind: int, widget: int, key: int, text: str, x: int, y: int) -> bool {
        return gui_post_event(self.handle, kind, widget, key, text, x, y);
    }
}

# Simple backend-independent layouts. `padding` is the outer inset and `gap`
# separates adjacent controls. Widgets are laid out in list order.
fn gui_vbox(widgets: List<GuiWidget>, x: int, y: int, width: int,
             row_height: int, gap: int) -> void {
    let mut top = y;
    for widget in widgets {
        widget.bounds(x, top, width, row_height);
        top = top + row_height + gap;
    }
}

fn gui_hbox(widgets: List<GuiWidget>, x: int, y: int, width: int,
             height: int, gap: int) -> void {
    let count = list_size(widgets);
    if count <= 0 { return; }
    let cell = (width - gap * (count - 1)) / count;
    let mut left = x;
    for widget in widgets {
        widget.bounds(left, y, cell, height);
        left = left + cell + gap;
    }
}

fn gui_grid(widgets: List<GuiWidget>, columns: int, x: int, y: int,
             width: int, row_height: int, gap: int) -> void {
    if columns <= 0 { return; }
    let cell = (width - gap * (columns - 1)) / columns;
    let mut index = 0;
    for widget in widgets {
        let column = index % columns;
        let row = index / columns;
        widget.bounds(x + column * (cell + gap), y + row * (row_height + gap),
                      cell, row_height);
        index = index + 1;
    }
}

# Run a conventional GUI event loop. The handler may be a capturing closure.
# Returning from the handler does not close the app; call window.close() when
# the application is finished.
fn gui_run(window: GuiWindow, handler: fn(GuiEvent) -> void) -> void {
    window.show();
    while window.open() {
        let event = window.poll(16);
        if event.kind != gui_event_none() { handler(event); }
        if event.kind == gui_event_close() { return; }
    }
}

fn gui_rgb(red: int, green: int, blue: int) -> int {
    let mut r = red; let mut g = green; let mut b = blue;
    if r < 0 { r = 0; } else if r > 255 { r = 255; }
    if g < 0 { g = 0; } else if g > 255 { g = 255; }
    if b < 0 { b = 0; } else if b > 255 { b = 255; }
    return r * 65536 + g * 256 + b;
}

fn gui_confirm(title: str, message: str) -> bool {
    if !gui_supported() || gui_is_headless() { return false; }
    let window = GuiWindow(title, 440, 180);
    let label = window.label(message);
    label.bounds(20, 24, 400, 70);
    let accept = window.button("Yes");
    let decline = window.button("No");
    accept.bounds(110, 110, 90, 32);
    decline.bounds(240, 110, 90, 32);
    window.show();
    while window.open() {
        let event = window.poll(50);
        if event.is_click() && event.widget == accept.handle { window.close(); return true; }
        if event.is_click() && event.widget == decline.handle { window.close(); return false; }
        if event.is_close() { return false; }
    }
    return false;
}
