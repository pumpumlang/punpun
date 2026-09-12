# PunPun 1.x GUI contract

## Scope

`std.gui` is a retained desktop UI toolkit whose semantic model is shared by the
C, bytecode and direct-native backends. The platform layer is responsible only
for creating native windows, drawing controls and translating OS messages into
the common event format.

Windows uses Win32/GDI. POSIX loads X11 dynamically and therefore also works on
Wayland desktops through XWayland. Absence of a supported display is reported
by `gui_supported()` instead of aborting program startup.

`PUNPUN_GUI_HEADLESS=1` makes the retained model available without an OS window.
This mode is part of the testing contract: widget state, layout, posted events
and event-loop behavior must stay backend-equivalent there.

## Handles and lifetime

Windows and widgets use small positive runtime handles rather than native HWND,
XID or pointer values. `0` is invalid. A widget belongs to exactly one window.
Closing a window marks it closed immediately; runtime cleanup is idempotent and
releases any remaining native resources.

The public retained state of a widget is:

- kind;
- x/y/width/height;
- text;
- integer value and range;
- visible/hidden;
- enabled/disabled.

Changing retained state updates the native peer when one exists.

## Built-in controls

The portable control set is intentionally small but sufficient for ordinary
application UIs:

1. label
2. button
3. text input
4. checkbox
5. slider
6. progress bar
7. panel
8. canvas

Layouts are library code rather than backend magic. `gui_vbox`, `gui_hbox` and
`gui_grid` deterministically assign bounds in list order, so layout behavior is
the same under every backend and in headless tests.

## Events

`GuiWindow.poll(timeout_ms)` yields a `GuiEvent`. Event kinds are stable integers:

- 0 none
- 1 close
- 2 click
- 3 change
- 4 key
- 5 text changed
- 6 mouse move
- 7 mouse down
- 8 mouse up
- 9 resize
- 10 paint

An event also carries window/widget handles, a key or button code, text payload,
and x/y coordinates where meaningful. `GuiWindow.post(...)` queues an
application-defined event using the same representation. Posted events are
FIFO for a given process and are useful for application messaging and
deterministic tests.

`gui_run(window, handler)` polls until the window closes and invokes the supplied
function value for non-empty events. Capturing closures are supported; ordinary
PunPun ownership rules still apply to whatever the handler captures.

## Canvas

A canvas supports RGB clear, rectangle, line and text drawing. Canvas drawing is
immediate-mode rather than retained as a display list. Programs that need
persistent custom rendering redraw on `paint` events. This keeps the runtime
small and avoids forcing one scene-graph design on every application.

## Threading and blocking

The retained widget model is designed for one UI/event-loop thread. Application
events may be posted from ordinary code, but native widget mutation should be
performed by the UI thread. `poll` may wait up to its timeout; headless polling
has the same timeout behavior.

Modal alerts are native where a display exists. `gui_confirm` is implemented
with the toolkit event loop. Headless mode makes alerts nonblocking no-ops and
returns false from confirm rather than waiting for an event that cannot arrive.

## Platform limits

The stable contract does not claim a native Wayland compositor backend or full
platform IME/accessibility integration yet. X11 text entry currently follows the
X11 key-string path; richer Unicode input methods are future platform work.
These limits do not change widget/layout/event semantics and must not cause a
headless or unsupported-display program to crash.
