/* Retained cross-platform GUI runtime for PunPun.
 *
 * The public language API works with small positive integer handles. Widgets are
 * retained in this runtime so layout/event semantics are identical on Win32 and
 * X11. The native backends only draw and translate OS messages into the shared
 * PunPun event model.
 */
#include "ppcrt.h"
#include "ppc_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PP_GUI_MAX_WINDOWS 64
#define PP_GUI_MAX_WIDGETS 1024
#define PP_GUI_TEXT_CAP 4096

/* Widget kinds, mirrored by std.gui. */
enum {
    PP_GUI_LABEL = 1,
    PP_GUI_BUTTON = 2,
    PP_GUI_INPUT = 3,
    PP_GUI_CHECKBOX = 4,
    PP_GUI_SLIDER = 5,
    PP_GUI_PROGRESS = 6,
    PP_GUI_PANEL = 7,
    PP_GUI_CANVAS = 8
};

/* Event kinds, mirrored by std.gui. */
enum {
    PP_GUI_EVENT_NONE = 0,
    PP_GUI_EVENT_CLOSE = 1,
    PP_GUI_EVENT_CLICK = 2,
    PP_GUI_EVENT_CHANGE = 3,
    PP_GUI_EVENT_KEY = 4,
    PP_GUI_EVENT_TEXT = 5,
    PP_GUI_EVENT_MOUSE_MOVE = 6,
    PP_GUI_EVENT_MOUSE_DOWN = 7,
    PP_GUI_EVENT_MOUSE_UP = 8,
    PP_GUI_EVENT_RESIZE = 9,
    PP_GUI_EVENT_PAINT = 10
};

typedef struct pp_gui_window_rec {
    int used;
    int open;
    int visible;
    int width;
    int height;
    int focus_widget;
    char *title;
    uintptr_t native;
} pp_gui_window_rec;

typedef struct pp_gui_widget_rec {
    int used;
    int window;
    int kind;
    int x, y, width, height;
    int visible;
    int enabled;
    int64_t value;
    int64_t minimum;
    int64_t maximum;
    char *text;
    uintptr_t native;
} pp_gui_widget_rec;

typedef struct pp_gui_event_rec {
    int type;
    int window;
    int widget;
    int64_t key;
    int64_t x;
    int64_t y;
    char text[64];
} pp_gui_event_rec;

static pp_gui_window_rec g_windows[PP_GUI_MAX_WINDOWS];
static pp_gui_widget_rec g_widgets[PP_GUI_MAX_WIDGETS];
static _Thread_local pp_gui_event_rec g_event;
static pp_gui_event_rec g_posted[256];
static unsigned g_post_head, g_post_tail;
static ppc_plat_mutex *g_post_lock;
static int g_gui_initialized;
static int g_gui_backend_ready;
static int g_gui_is_headless;

static char *pp_gui_strdup(const char *text) {
    const char *source = text ? text : "";
    size_t n = strlen(source);
    char *copy = (char *)malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, source, n + 1);
    return copy;
}

static int pp_gui_window_valid(int64_t handle) {
    return handle > 0 && handle < PP_GUI_MAX_WINDOWS && g_windows[handle].used;
}
static int pp_gui_widget_valid(int64_t handle) {
    return handle > 0 && handle < PP_GUI_MAX_WIDGETS && g_widgets[handle].used;
}

static int pp_gui_alloc_window(void) {
    for (int i = 1; i < PP_GUI_MAX_WINDOWS; ++i) {
        if (!g_windows[i].used) {
            memset(&g_windows[i], 0, sizeof(g_windows[i]));
            g_windows[i].used = 1;
            g_windows[i].open = 1;
            return i;
        }
    }
    return 0;
}
static int pp_gui_alloc_widget(void) {
    for (int i = 1; i < PP_GUI_MAX_WIDGETS; ++i) {
        if (!g_widgets[i].used) {
            memset(&g_widgets[i], 0, sizeof(g_widgets[i]));
            g_widgets[i].used = 1;
            g_widgets[i].visible = 1;
            g_widgets[i].enabled = 1;
            g_widgets[i].minimum = 0;
            g_widgets[i].maximum = 100;
            return i;
        }
    }
    return 0;
}

static void pp_gui_set_event(int type, int window, int widget, int64_t key,
                             int64_t x, int64_t y, const char *text) {
    memset(&g_event, 0, sizeof(g_event));
    g_event.type = type;
    g_event.window = window;
    g_event.widget = widget;
    g_event.key = key;
    g_event.x = x;
    g_event.y = y;
    if (text) {
        strncpy(g_event.text, text, sizeof(g_event.text) - 1);
        g_event.text[sizeof(g_event.text) - 1] = '\0';
    }
}

static int pp_gui_pop_posted(int window) {
    if (!g_post_lock) return 0;
    ppc_plat_mutex_lock(g_post_lock);
    if (g_post_head == g_post_tail) { ppc_plat_mutex_unlock(g_post_lock); return 0; }
    unsigned index = g_post_head;
    unsigned found = 256;
    while (index != g_post_tail) {
        if (g_posted[index].window == window) { found = index; break; }
        index = (index + 1u) & 255u;
    }
    if (found == 256) { ppc_plat_mutex_unlock(g_post_lock); return 0; }
    g_event = g_posted[found];
    /* Compact the tiny queue; posting is for application/custom events, not bulk I/O. */
    while (found != g_post_tail) {
        unsigned next = (found + 1u) & 255u;
        if (next == g_post_tail) break;
        g_posted[found] = g_posted[next];
        found = next;
    }
    g_post_tail = (g_post_tail - 1u) & 255u;
    ppc_plat_mutex_unlock(g_post_lock);
    return g_event.type;
}

bool pp_gui_post_event(int64_t window, int64_t type, int64_t widget, int64_t key,
                       const char *text, int64_t x, int64_t y) {
    if (!pp_gui_window_valid(window) || type <= 0) return false;
    if (widget != 0 && !pp_gui_widget_valid(widget)) return false;
    if (!g_post_lock) g_post_lock = ppc_plat_mutex_new();
    ppc_plat_mutex_lock(g_post_lock);
    unsigned next = (g_post_tail + 1u) & 255u;
    if (next == g_post_head) { ppc_plat_mutex_unlock(g_post_lock); return false; }
    pp_gui_event_rec *event = &g_posted[g_post_tail];
    memset(event, 0, sizeof(*event));
    event->type = (int)type; event->window = (int)window; event->widget = (int)widget;
    event->key = key; event->x = x; event->y = y;
    if (text) { strncpy(event->text, text, sizeof(event->text)-1); event->text[sizeof(event->text)-1]='\0'; }
    g_post_tail = next;
    ppc_plat_mutex_unlock(g_post_lock);
    return true;
}

static int pp_gui_hit_test(int window, int x, int y) {
    /* Reverse creation order gives later widgets natural z-order. */
    for (int i = PP_GUI_MAX_WIDGETS - 1; i > 0; --i) {
        pp_gui_widget_rec *w = &g_widgets[i];
        if (!w->used || w->window != window || !w->visible || !w->enabled) continue;
        if (x >= w->x && y >= w->y && x < w->x + w->width && y < w->y + w->height)
            return i;
    }
    return 0;
}

/* Platform hooks implemented below. */
static void pp_gui_platform_runtime_cleanup(void);
static int pp_gui_platform_window_create(int handle);
static void pp_gui_platform_window_destroy(int handle);
static void pp_gui_platform_window_show(int handle, int visible);
static void pp_gui_platform_window_title(int handle);
static int pp_gui_platform_widget_create(int handle);
static void pp_gui_platform_widget_destroy(int handle);
static void pp_gui_platform_widget_sync(int handle);
static void pp_gui_platform_redraw(int window);
static int pp_gui_platform_poll(int window, int64_t timeout_ms);
static int pp_gui_platform_canvas_clear(int widget, int64_t rgb);
static int pp_gui_platform_canvas_rect(int widget, int x, int y, int width, int height,
                                       int64_t rgb, int filled);
static int pp_gui_platform_canvas_line(int widget, int x1, int y1, int x2, int y2, int64_t rgb);
static int pp_gui_platform_canvas_text(int widget, int x, int y, const char *text, int64_t rgb);

void pp_gui_runtime_cleanup(void) {
    if (g_post_lock) { ppc_plat_mutex_free(g_post_lock); g_post_lock = NULL; }
    g_post_head = g_post_tail = 0;
    for (int i = 1; i < PP_GUI_MAX_WIDGETS; ++i) {
        if (!g_widgets[i].used) continue;
        pp_gui_platform_widget_destroy(i);
        free(g_widgets[i].text);
        memset(&g_widgets[i], 0, sizeof(g_widgets[i]));
    }
    for (int i = 1; i < PP_GUI_MAX_WINDOWS; ++i) {
        if (!g_windows[i].used) continue;
        pp_gui_platform_window_destroy(i);
        free(g_windows[i].title);
        memset(&g_windows[i], 0, sizeof(g_windows[i]));
    }
    pp_gui_platform_runtime_cleanup();
}

bool pp_gui_available(void) {
    if (!g_gui_initialized) pp_gui_runtime_init();
    return g_gui_backend_ready != 0;
}
bool pp_gui_headless(void) {
    if (!g_gui_initialized) pp_gui_runtime_init();
    return g_gui_is_headless != 0;
}

int64_t pp_gui_window_create(const char *title, int64_t width, int64_t height) {
    if (!pp_gui_available()) return 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width > 16384) width = 16384;
    if (height > 16384) height = 16384;
    int handle = pp_gui_alloc_window();
    if (!handle) return 0;
    g_windows[handle].width = (int)width;
    g_windows[handle].height = (int)height;
    g_windows[handle].title = pp_gui_strdup(title ? title : "PunPun");
    if (!pp_gui_platform_window_create(handle)) {
        free(g_windows[handle].title);
        memset(&g_windows[handle], 0, sizeof(g_windows[handle]));
        return 0;
    }
    return handle;
}

bool pp_gui_window_show(int64_t handle, bool visible) {
    if (!pp_gui_window_valid(handle)) return false;
    g_windows[handle].visible = visible ? 1 : 0;
    pp_gui_platform_window_show((int)handle, visible ? 1 : 0);
    return true;
}

bool pp_gui_window_close(int64_t handle) {
    if (!pp_gui_window_valid(handle)) return false;
    g_windows[handle].open = 0;
    pp_gui_platform_window_destroy((int)handle);
    g_windows[handle].native = 0;
    return true;
}

bool pp_gui_window_open(int64_t handle) {
    return pp_gui_window_valid(handle) && g_windows[handle].open;
}

bool pp_gui_window_set_title(int64_t handle, const char *title) {
    if (!pp_gui_window_valid(handle)) return false;
    char *copy = pp_gui_strdup(title);
    if (!copy) return false;
    free(g_windows[handle].title);
    g_windows[handle].title = copy;
    pp_gui_platform_window_title((int)handle);
    return true;
}

int64_t pp_gui_window_width(int64_t handle) {
    return pp_gui_window_valid(handle) ? g_windows[handle].width : 0;
}
int64_t pp_gui_window_height(int64_t handle) {
    return pp_gui_window_valid(handle) ? g_windows[handle].height : 0;
}

int64_t pp_gui_widget_create(int64_t window, int64_t kind, const char *text) {
    if (!pp_gui_window_valid(window)) return 0;
    if (kind < PP_GUI_LABEL || kind > PP_GUI_CANVAS) return 0;
    int handle = pp_gui_alloc_widget();
    if (!handle) return 0;
    pp_gui_widget_rec *w = &g_widgets[handle];
    w->window = (int)window;
    w->kind = (int)kind;
    w->text = pp_gui_strdup(text);
    w->width = 100;
    w->height = kind == PP_GUI_INPUT ? 28 : 24;
    if (!w->text || !pp_gui_platform_widget_create(handle)) {
        free(w->text);
        memset(w, 0, sizeof(*w));
        return 0;
    }
    return handle;
}

bool pp_gui_widget_destroy(int64_t handle) {
    if (!pp_gui_widget_valid(handle)) return false;
    pp_gui_platform_widget_destroy((int)handle);
    free(g_widgets[handle].text);
    memset(&g_widgets[handle], 0, sizeof(g_widgets[handle]));
    return true;
}

bool pp_gui_widget_set_bounds(int64_t handle, int64_t x, int64_t y,
                              int64_t width, int64_t height) {
    if (!pp_gui_widget_valid(handle)) return false;
    pp_gui_widget_rec *w = &g_widgets[handle];
    w->x = (int)x; w->y = (int)y;
    w->width = width < 1 ? 1 : (int)width;
    w->height = height < 1 ? 1 : (int)height;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}

int64_t pp_gui_widget_x(int64_t handle) { return pp_gui_widget_valid(handle) ? g_widgets[handle].x : 0; }
int64_t pp_gui_widget_y(int64_t handle) { return pp_gui_widget_valid(handle) ? g_widgets[handle].y : 0; }
int64_t pp_gui_widget_width(int64_t handle) { return pp_gui_widget_valid(handle) ? g_widgets[handle].width : 0; }
int64_t pp_gui_widget_height(int64_t handle) { return pp_gui_widget_valid(handle) ? g_widgets[handle].height : 0; }

bool pp_gui_widget_set_text(int64_t handle, const char *text) {
    if (!pp_gui_widget_valid(handle)) return false;
    char *copy = pp_gui_strdup(text);
    if (!copy) return false;
    free(g_widgets[handle].text);
    g_widgets[handle].text = copy;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}

const char *pp_gui_widget_text(int64_t handle) {
    return pp_gui_widget_valid(handle) && g_widgets[handle].text ? g_widgets[handle].text : "";
}

bool pp_gui_widget_set_value(int64_t handle, int64_t value) {
    if (!pp_gui_widget_valid(handle)) return false;
    pp_gui_widget_rec *w = &g_widgets[handle];
    if (value < w->minimum) value = w->minimum;
    if (value > w->maximum) value = w->maximum;
    w->value = value;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}
int64_t pp_gui_widget_value(int64_t handle) {
    return pp_gui_widget_valid(handle) ? g_widgets[handle].value : 0;
}

bool pp_gui_widget_set_range(int64_t handle, int64_t minimum, int64_t maximum) {
    if (!pp_gui_widget_valid(handle) || maximum <= minimum) return false;
    pp_gui_widget_rec *w = &g_widgets[handle];
    w->minimum = minimum; w->maximum = maximum;
    if (w->value < minimum) w->value = minimum;
    if (w->value > maximum) w->value = maximum;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}

bool pp_gui_widget_set_visible(int64_t handle, bool visible) {
    if (!pp_gui_widget_valid(handle)) return false;
    g_widgets[handle].visible = visible ? 1 : 0;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}
bool pp_gui_widget_set_enabled(int64_t handle, bool enabled) {
    if (!pp_gui_widget_valid(handle)) return false;
    g_widgets[handle].enabled = enabled ? 1 : 0;
    pp_gui_platform_widget_sync((int)handle);
    return true;
}

bool pp_gui_redraw(int64_t window) {
    if (!pp_gui_window_valid(window)) return false;
    pp_gui_platform_redraw((int)window);
    return true;
}

int64_t pp_gui_poll(int64_t window, int64_t timeout_ms) {
    memset(&g_event, 0, sizeof(g_event));
    if (!pp_gui_window_valid(window) || !g_windows[window].open) return PP_GUI_EVENT_CLOSE;
    if (timeout_ms < 0) timeout_ms = 0;
    int posted = pp_gui_pop_posted((int)window);
    if (posted) return posted;
    return pp_gui_platform_poll((int)window, timeout_ms);
}
int64_t pp_gui_event_window(void) { return g_event.window; }
int64_t pp_gui_event_widget(void) { return g_event.widget; }
int64_t pp_gui_event_key(void) { return g_event.key; }
int64_t pp_gui_event_x(void) { return g_event.x; }
int64_t pp_gui_event_y(void) { return g_event.y; }
const char *pp_gui_event_text(void) { return g_event.text; }

bool pp_gui_canvas_clear(int64_t widget, int64_t rgb) {
    return pp_gui_widget_valid(widget) && g_widgets[widget].kind == PP_GUI_CANVAS &&
           pp_gui_platform_canvas_clear((int)widget, rgb);
}
bool pp_gui_canvas_rect(int64_t widget, int64_t x, int64_t y, int64_t width, int64_t height,
                        int64_t rgb, bool filled) {
    return pp_gui_widget_valid(widget) && g_widgets[widget].kind == PP_GUI_CANVAS &&
           pp_gui_platform_canvas_rect((int)widget, (int)x, (int)y, (int)width, (int)height,
                                       rgb, filled ? 1 : 0);
}
bool pp_gui_canvas_line(int64_t widget, int64_t x1, int64_t y1, int64_t x2, int64_t y2,
                        int64_t rgb) {
    return pp_gui_widget_valid(widget) && g_widgets[widget].kind == PP_GUI_CANVAS &&
           pp_gui_platform_canvas_line((int)widget, (int)x1, (int)y1, (int)x2, (int)y2, rgb);
}
bool pp_gui_canvas_text(int64_t widget, int64_t x, int64_t y, const char *text, int64_t rgb) {
    return pp_gui_widget_valid(widget) && g_widgets[widget].kind == PP_GUI_CANVAS &&
           pp_gui_platform_canvas_text((int)widget, (int)x, (int)y, text, rgb);
}

#if PPC_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

static const char *PP_GUI_CLASS = "PunPunGuiWindow";
static HINSTANCE g_gui_instance;

static int pp_gui_find_window_native(HWND hwnd) {
    for (int i = 1; i < PP_GUI_MAX_WINDOWS; ++i)
        if (g_windows[i].used && (HWND)g_windows[i].native == hwnd) return i;
    return 0;
}
static int pp_gui_find_widget_native(HWND hwnd) {
    for (int i = 1; i < PP_GUI_MAX_WIDGETS; ++i)
        if (g_widgets[i].used && (HWND)g_widgets[i].native == hwnd) return i;
    return 0;
}

static LRESULT CALLBACK pp_gui_wndproc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    int window = pp_gui_find_window_native(hwnd);
    if (message == WM_CLOSE) {
        if (window) {
            g_windows[window].open = 0;
            pp_gui_set_event(PP_GUI_EVENT_CLOSE, window, 0, 0, 0, 0, NULL);
        }
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_SIZE && window) {
        g_windows[window].width = LOWORD(lp);
        g_windows[window].height = HIWORD(lp);
        pp_gui_set_event(PP_GUI_EVENT_RESIZE, window, 0, 0,
                         g_windows[window].width, g_windows[window].height, NULL);
    }
    if (message == WM_COMMAND && window) {
        HWND child = (HWND)lp;
        int widget = pp_gui_find_widget_native(child);
        if (widget) {
            int code = HIWORD(wp);
            if (code == BN_CLICKED) {
                if (g_widgets[widget].kind == PP_GUI_CHECKBOX) {
                    LRESULT checked = SendMessageA(child, BM_GETCHECK, 0, 0);
                    g_widgets[widget].value = checked == BST_CHECKED ? 1 : 0;
                    pp_gui_set_event(PP_GUI_EVENT_CHANGE, window, widget, 0, 0, 0, NULL);
                } else {
                    pp_gui_set_event(PP_GUI_EVENT_CLICK, window, widget, 0, 0, 0, NULL);
                }
            } else if (code == STN_CLICKED) {
                pp_gui_widget_rec *w = &g_widgets[widget];
                POINT point; GetCursorPos(&point);
                HWND parent = (HWND)g_windows[window].native;
                ScreenToClient(parent, &point);
                if (w->kind == PP_GUI_SLIDER) {
                    int rel = point.x - w->x;
                    if (rel < 0) rel = 0;
                    if (rel > w->width) rel = w->width;
                    w->value = w->minimum + (w->maximum - w->minimum) * rel / (w->width ? w->width : 1);
                    pp_gui_set_event(PP_GUI_EVENT_CHANGE, window, widget, 0, point.x, point.y, NULL);
                } else {
                    pp_gui_set_event(PP_GUI_EVENT_CLICK, window, widget, 0, point.x, point.y, NULL);
                }
            } else if (code == EN_CHANGE && g_widgets[widget].kind == PP_GUI_INPUT) {
                int len = GetWindowTextLengthA(child);
                if (len >= PP_GUI_TEXT_CAP) len = PP_GUI_TEXT_CAP - 1;
                char *buffer = (char *)malloc((size_t)len + 1);
                if (buffer) {
                    GetWindowTextA(child, buffer, len + 1);
                    free(g_widgets[widget].text);
                    g_widgets[widget].text = buffer;
                    pp_gui_set_event(PP_GUI_EVENT_TEXT, window, widget, 0, 0, 0, buffer);
                }
            }
        }
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

void pp_gui_runtime_init(void) {
    if (g_gui_initialized) return;
    g_gui_initialized = 1;
    if (!g_post_lock) g_post_lock = ppc_plat_mutex_new();
    const char *headless = getenv("PUNPUN_GUI_HEADLESS");
    if (headless && strcmp(headless, "1") == 0) { g_gui_is_headless = 1; g_gui_backend_ready = 1; return; }
    g_gui_instance = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = pp_gui_wndproc;
    wc.hInstance = g_gui_instance;
    wc.lpszClassName = PP_GUI_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (RegisterClassA(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        g_gui_backend_ready = 1;
}

static void pp_gui_platform_runtime_cleanup(void) {}
static int pp_gui_platform_window_create(int handle) {
    if (g_gui_is_headless) return 1;
    pp_gui_window_rec *w = &g_windows[handle];
    HWND hwnd = CreateWindowExA(0, PP_GUI_CLASS, w->title ? w->title : "PunPun",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                w->width, w->height, NULL, NULL, g_gui_instance, NULL);
    if (!hwnd) return 0;
    w->native = (uintptr_t)hwnd;
    return 1;
}
static void pp_gui_platform_window_destroy(int handle) {
    if (g_gui_is_headless) return;
    HWND hwnd = (HWND)g_windows[handle].native;
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}
static void pp_gui_platform_window_show(int handle, int visible) {
    if (g_gui_is_headless) return;
    HWND hwnd = (HWND)g_windows[handle].native;
    if (hwnd) { ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE); UpdateWindow(hwnd); }
}
static void pp_gui_platform_window_title(int handle) {
    if (g_gui_is_headless) return;
    HWND hwnd = (HWND)g_windows[handle].native;
    if (hwnd) SetWindowTextA(hwnd, g_windows[handle].title ? g_windows[handle].title : "PunPun");
}
static int pp_gui_platform_widget_create(int handle) {
    if (g_gui_is_headless) return 1;
    pp_gui_widget_rec *w = &g_widgets[handle];
    HWND parent = (HWND)g_windows[w->window].native;
    const char *klass = "STATIC";
    DWORD style = WS_CHILD | WS_VISIBLE;
    if (w->kind == PP_GUI_BUTTON) { klass = "BUTTON"; style |= BS_PUSHBUTTON; }
    else if (w->kind == PP_GUI_INPUT) { klass = "EDIT"; style |= WS_BORDER | ES_AUTOHSCROLL; }
    else if (w->kind == PP_GUI_CHECKBOX) { klass = "BUTTON"; style |= BS_AUTOCHECKBOX; }
    else if (w->kind == PP_GUI_SLIDER || w->kind == PP_GUI_PANEL || w->kind == PP_GUI_CANVAS) { klass = "STATIC"; style |= SS_NOTIFY | WS_BORDER; }
    else if (w->kind == PP_GUI_PROGRESS) { klass = "STATIC"; style |= WS_BORDER; }
    HWND child = CreateWindowExA(0, klass, w->text ? w->text : "", style,
                                 w->x, w->y, w->width, w->height, parent,
                                 (HMENU)(INT_PTR)handle, g_gui_instance, NULL);
    w->native = (uintptr_t)child;
    return child != NULL;
}
static void pp_gui_platform_widget_destroy(int handle) {
    if (g_gui_is_headless) return;
    HWND hwnd = (HWND)g_widgets[handle].native;
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}
static void pp_gui_win_draw_value_widget(int handle) {
    pp_gui_widget_rec *w = &g_widgets[handle];
    if (g_gui_is_headless || !w->native) return;
    if (w->kind != PP_GUI_SLIDER && w->kind != PP_GUI_PROGRESS) return;
    HWND hwnd = (HWND)w->native;
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    RECT r; GetClientRect(hwnd, &r);
    HBRUSH bg = CreateSolidBrush(RGB(245,245,245));
    FillRect(dc, &r, bg); DeleteObject(bg);
    int span = (int)(w->maximum - w->minimum);
    int inner = (r.right - r.left) - 4;
    int fill = span > 0 ? (int)(((w->value - w->minimum) * inner) / span) : 0;
    if (fill < 0) fill = 0;
    if (fill > inner) fill = inner;
    RECT bar = {2, 2, 2 + fill, (r.bottom - r.top) - 2};
    HBRUSH accent = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    FillRect(dc, &bar, accent); DeleteObject(accent);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (w->kind == PP_GUI_SLIDER) {
        int knob = 2 + fill;
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(32,32,32));
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, knob, 2, NULL); LineTo(dc, knob, r.bottom - 2);
        SelectObject(dc, old); DeleteObject(pen);
    }
    ReleaseDC(hwnd, dc);
}
static void pp_gui_platform_widget_sync(int handle) {
    if (g_gui_is_headless) return;
    pp_gui_widget_rec *w = &g_widgets[handle];
    HWND hwnd = (HWND)w->native;
    if (!hwnd) return;
    MoveWindow(hwnd, w->x, w->y, w->width, w->height, TRUE);
    SetWindowTextA(hwnd, w->text ? w->text : "");
    ShowWindow(hwnd, w->visible ? SW_SHOW : SW_HIDE);
    EnableWindow(hwnd, w->enabled ? TRUE : FALSE);
    if (w->kind == PP_GUI_CHECKBOX)
        SendMessageA(hwnd, BM_SETCHECK, w->value ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    pp_gui_win_draw_value_widget(handle);
}
static void pp_gui_platform_redraw(int window) {
    if (g_gui_is_headless) return;
    HWND hwnd = (HWND)g_windows[window].native;
    if (hwnd) {
        InvalidateRect(hwnd, NULL, TRUE); UpdateWindow(hwnd);
        for (int i = 1; i < PP_GUI_MAX_WIDGETS; ++i)
            if (g_widgets[i].used && g_widgets[i].window == window) pp_gui_win_draw_value_widget(i);
    }
}
static int pp_gui_platform_poll(int window, int64_t timeout_ms) {
    if (g_gui_is_headless) { if (timeout_ms > 0) ppc_plat_sleep_ms(timeout_ms); return 0; }
    uint64_t start = GetTickCount64();
    for (;;) {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
            if (g_event.type) return g_event.type;
        }
        if (!g_windows[window].open) return PP_GUI_EVENT_CLOSE;
        if (timeout_ms == 0 || (int64_t)(GetTickCount64() - start) >= timeout_ms) return 0;
        Sleep(1);
    }
}
static COLORREF pp_gui_win_color(int64_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static int pp_gui_platform_canvas_clear(int widget, int64_t rgb) {
    if (g_gui_is_headless) return 1;
    HWND hwnd = (HWND)g_widgets[widget].native; if (!hwnd) return 0;
    HDC dc = GetDC(hwnd); RECT r; GetClientRect(hwnd, &r);
    HBRUSH brush = CreateSolidBrush(pp_gui_win_color(rgb)); FillRect(dc, &r, brush);
    DeleteObject(brush); ReleaseDC(hwnd, dc); return 1;
}
static int pp_gui_platform_canvas_rect(int widget, int x, int y, int width, int height, int64_t rgb, int filled) {
    if (g_gui_is_headless) return 1;
    HWND hwnd=(HWND)g_widgets[widget].native; if(!hwnd)return 0; HDC dc=GetDC(hwnd);
    HPEN pen=CreatePen(PS_SOLID,1,pp_gui_win_color(rgb)); HGDIOBJ oldp=SelectObject(dc,pen);
    HBRUSH brush=filled?CreateSolidBrush(pp_gui_win_color(rgb)):(HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ oldb=SelectObject(dc,brush); Rectangle(dc,x,y,x+width,y+height);
    SelectObject(dc,oldb); SelectObject(dc,oldp); if(filled)DeleteObject(brush); DeleteObject(pen); ReleaseDC(hwnd,dc); return 1;
}
static int pp_gui_platform_canvas_line(int widget, int x1, int y1, int x2, int y2, int64_t rgb) {
    if (g_gui_is_headless) return 1;
    HWND hwnd=(HWND)g_widgets[widget].native; if(!hwnd)return 0; HDC dc=GetDC(hwnd);
    HPEN pen=CreatePen(PS_SOLID,1,pp_gui_win_color(rgb)); HGDIOBJ old=SelectObject(dc,pen);
    MoveToEx(dc,x1,y1,NULL); LineTo(dc,x2,y2); SelectObject(dc,old); DeleteObject(pen); ReleaseDC(hwnd,dc); return 1;
}
static int pp_gui_platform_canvas_text(int widget, int x, int y, const char *text, int64_t rgb) {
    if (g_gui_is_headless) return 1;
    HWND hwnd=(HWND)g_widgets[widget].native; if(!hwnd)return 0; HDC dc=GetDC(hwnd);
    SetTextColor(dc,pp_gui_win_color(rgb)); SetBkMode(dc,TRANSPARENT); const char *s=text?text:"";
    TextOutA(dc,x,y,s,(int)strlen(s)); ReleaseDC(hwnd,dc); return 1;
}

bool pp_gui_message(const char *title, const char *message) {
    if (g_gui_is_headless) return true;
    return MessageBoxA(NULL, message ? message : "", title ? title : "PunPun",
                       MB_OK | MB_ICONINFORMATION) == IDOK;
}

#else
/* X11 backend loaded dynamically so PunPun still has no X11 build dependency. */
#  include <dlfcn.h>
#  include <time.h>
#  include <unistd.h>

typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef unsigned long Time;
typedef unsigned long KeySym;
typedef unsigned long Atom;
typedef struct _XGC *GC;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display; Window window;
} pp_x_any_event;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display;
    Window window, root, subwindow; Time time;
    int x, y, x_root, y_root; unsigned int state; unsigned int button; int same_screen;
} pp_x_button_event;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display;
    Window window, root, subwindow; Time time;
    int x, y, x_root, y_root; unsigned int state; unsigned int keycode; int same_screen;
} pp_x_key_event;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display;
    Window window; int x, y, width, height, count;
} pp_x_expose_event;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display;
    Window event, window; int x, y, width, height, border_width; Window above; int override_redirect;
} pp_x_configure_event;
typedef struct {
    int type; unsigned long serial; int send_event; Display *display; Window window;
    Atom message_type; int format; union { char b[20]; short s[10]; long l[5]; } data;
} pp_x_client_event;
typedef union {
    int type;
    pp_x_any_event xany;
    pp_x_button_event xbutton;
    pp_x_key_event xkey;
    pp_x_expose_event xexpose;
    pp_x_configure_event xconfigure;
    pp_x_client_event xclient;
    long pad[24];
} XEvent;

enum { PP_X_KEY_PRESS=2, PP_X_BUTTON_PRESS=4, PP_X_BUTTON_RELEASE=5, PP_X_MOTION=6,
       PP_X_EXPOSE=12, PP_X_DESTROY_NOTIFY=17, PP_X_CONFIGURE_NOTIFY=22, PP_X_CLIENT_MESSAGE=33 };

typedef Display *(*x_open_fn)(const char *);
typedef int (*x_close_fn)(Display *);
typedef int (*x_default_screen_fn)(Display *);
typedef Window (*x_root_fn)(Display *, int);
typedef unsigned long (*x_pixel_fn)(Display *, int);
typedef Window (*x_create_fn)(Display *, Window, int, int, unsigned int, unsigned int,
                              unsigned int, unsigned long, unsigned long);
typedef int (*x_store_name_fn)(Display *, Window, const char *);
typedef int (*x_select_fn)(Display *, Window, long);
typedef int (*x_map_fn)(Display *, Window);
typedef int (*x_unmap_fn)(Display *, Window);
typedef int (*x_pending_fn)(Display *);
typedef int (*x_next_fn)(Display *, XEvent *);
typedef int (*x_destroy_fn)(Display *, Window);
typedef int (*x_flush_fn)(Display *);
typedef GC (*x_gc_fn)(Display *, int);
typedef int (*x_foreground_fn)(Display *, GC, unsigned long);
typedef int (*x_draw_string_fn)(Display *, Window, GC, int, int, const char *, int);
typedef int (*x_draw_rect_fn)(Display *, Window, GC, int, int, unsigned int, unsigned int);
typedef int (*x_fill_rect_fn)(Display *, Window, GC, int, int, unsigned int, unsigned int);
typedef int (*x_draw_line_fn)(Display *, Window, GC, int, int, int, int);
typedef int (*x_clear_fn)(Display *, Window);
typedef int (*x_lookup_string_fn)(pp_x_key_event *, char *, int, KeySym *, void *);
typedef int (*x_init_threads_fn)(void);
typedef Atom (*x_intern_atom_fn)(Display *, const char *, int);
typedef int (*x_set_wm_protocols_fn)(Display *, Window, Atom *, int);

static void *g_x11;
static Display *g_display;
static int g_screen;
static unsigned long g_black, g_white;
static x_open_fn x_open; static x_close_fn x_close; static x_default_screen_fn x_screen;
static x_root_fn x_root; static x_pixel_fn x_black; static x_pixel_fn x_white;
static x_create_fn x_create; static x_store_name_fn x_store_name; static x_select_fn x_select;
static x_map_fn x_map; static x_unmap_fn x_unmap; static x_pending_fn x_pending; static x_next_fn x_next;
static x_destroy_fn x_destroy; static x_flush_fn x_flush; static x_gc_fn x_gc;
static x_foreground_fn x_foreground; static x_draw_string_fn x_draw_string;
static x_draw_rect_fn x_draw_rect; static x_fill_rect_fn x_fill_rect; static x_draw_line_fn x_draw_line;
static x_clear_fn x_clear; static x_lookup_string_fn x_lookup_string;
static x_intern_atom_fn x_intern_atom; static x_set_wm_protocols_fn x_set_wm_protocols;
static Atom g_wm_delete;

static int pp_gui_bind(void **slot, const char *name) { *slot=dlsym(g_x11,name); return *slot!=NULL; }
#define X_BIND(target, name) do { if(!pp_gui_bind((void **)&(target),(name))) return; } while(0)

void pp_gui_runtime_init(void) {
    if (g_gui_initialized) return;
    g_gui_initialized = 1;
    if (!g_post_lock) g_post_lock = ppc_plat_mutex_new();
    const char *headless = getenv("PUNPUN_GUI_HEADLESS");
    if (headless && strcmp(headless, "1") == 0) { g_gui_is_headless = 1; g_gui_backend_ready = 1; return; }
    g_x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!g_x11) g_x11 = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_x11) return;
    X_BIND(x_open,"XOpenDisplay"); X_BIND(x_close,"XCloseDisplay"); X_BIND(x_screen,"XDefaultScreen");
    X_BIND(x_root,"XRootWindow"); X_BIND(x_black,"XBlackPixel"); X_BIND(x_white,"XWhitePixel");
    X_BIND(x_create,"XCreateSimpleWindow"); X_BIND(x_store_name,"XStoreName"); X_BIND(x_select,"XSelectInput");
    X_BIND(x_map,"XMapWindow"); X_BIND(x_unmap,"XUnmapWindow"); X_BIND(x_pending,"XPending");
    X_BIND(x_next,"XNextEvent"); X_BIND(x_destroy,"XDestroyWindow"); X_BIND(x_flush,"XFlush");
    X_BIND(x_gc,"XDefaultGC"); X_BIND(x_foreground,"XSetForeground"); X_BIND(x_draw_string,"XDrawString");
    X_BIND(x_draw_rect,"XDrawRectangle"); X_BIND(x_fill_rect,"XFillRectangle"); X_BIND(x_draw_line,"XDrawLine");
    X_BIND(x_clear,"XClearWindow"); X_BIND(x_lookup_string,"XLookupString");
    X_BIND(x_intern_atom,"XInternAtom"); X_BIND(x_set_wm_protocols,"XSetWMProtocols");
    x_init_threads_fn init_threads=NULL; (void)pp_gui_bind((void **)&init_threads,"XInitThreads");
    if(init_threads)(void)init_threads();
    g_display=x_open(NULL); if(!g_display)return;
    g_screen=x_screen(g_display); g_black=x_black(g_display,g_screen); g_white=x_white(g_display,g_screen);
    g_wm_delete = x_intern_atom(g_display, "WM_DELETE_WINDOW", 0);
    g_gui_backend_ready=1;
}
#undef X_BIND

static int pp_gui_find_window_native(Window native) {
    for(int i=1;i<PP_GUI_MAX_WINDOWS;++i) if(g_windows[i].used && (Window)g_windows[i].native==native)return i;
    return 0;
}
static unsigned long pp_gui_x_color(int64_t rgb) {
    /* TrueColor visuals on mainstream X11 use 0xRRGGBB pixel values. */
    return (unsigned long)(rgb & 0xFFFFFF);
}
static void pp_gui_draw_widget(int handle) {
    pp_gui_widget_rec *w=&g_widgets[handle]; if(!w->used||!w->visible||!g_display)return;
    Window win=(Window)g_windows[w->window].native; if(!win)return; GC gc=x_gc(g_display,g_screen);
    x_foreground(g_display,gc,g_black);
    if(w->kind==PP_GUI_PANEL||w->kind==PP_GUI_CANVAS) {
        x_draw_rect(g_display,win,gc,w->x,w->y,(unsigned)w->width,(unsigned)w->height); return;
    }
    if(w->kind==PP_GUI_BUTTON||w->kind==PP_GUI_INPUT||w->kind==PP_GUI_CHECKBOX||w->kind==PP_GUI_SLIDER||w->kind==PP_GUI_PROGRESS)
        x_draw_rect(g_display,win,gc,w->x,w->y,(unsigned)w->width,(unsigned)w->height);
    if(w->kind==PP_GUI_CHECKBOX) {
        int box=16; x_draw_rect(g_display,win,gc,w->x+4,w->y+(w->height-box)/2,(unsigned)box,(unsigned)box);
        if(w->value) { x_draw_line(g_display,win,gc,w->x+7,w->y+w->height/2,w->x+11,w->y+w->height/2+4); x_draw_line(g_display,win,gc,w->x+11,w->y+w->height/2+4,w->x+18,w->y+w->height/2-5); }
    }
    if(w->kind==PP_GUI_SLIDER||w->kind==PP_GUI_PROGRESS) {
        int64_t span=w->maximum-w->minimum; int fill=span>0?(int)(((w->value-w->minimum)*(w->width-4))/span):0;
        if (fill < 0) fill = 0;
        if (fill > w->width - 4) fill = w->width - 4;
        x_fill_rect(g_display,win,gc,w->x+2,w->y+2,(unsigned)fill,(unsigned)(w->height-4));
    }
    const char *text=w->text?w->text:""; int tx=w->x+6;
    if(w->kind==PP_GUI_CHECKBOX)tx=w->x+26;
    if(*text)x_draw_string(g_display,win,gc,tx,w->y+w->height/2+5,text,(int)strlen(text));
}
static void pp_gui_platform_runtime_cleanup(void) {
    if (g_display && x_close) { x_close(g_display); g_display = NULL; }
    if (g_x11) { dlclose(g_x11); g_x11 = NULL; }
    g_gui_backend_ready = 0;
}
static void pp_gui_platform_redraw(int window) {
    if (g_gui_is_headless) return;
    if (!g_display || !g_windows[window].native) return;
    Window win = (Window)g_windows[window].native;
    x_clear(g_display,win); for(int i=1;i<PP_GUI_MAX_WIDGETS;++i)if(g_widgets[i].used&&g_widgets[i].window==window)pp_gui_draw_widget(i); x_flush(g_display);
}
static int pp_gui_platform_window_create(int handle) {
    if (g_gui_is_headless) return 1;
    if (!g_display) return 0;
    pp_gui_window_rec *w = &g_windows[handle];
    Window win=x_create(g_display,x_root(g_display,g_screen),80,80,(unsigned)w->width,(unsigned)w->height,1,g_black,g_white);
    if (!win) return 0;
    w->native = (uintptr_t)win;
    x_store_name(g_display, win, w->title ? w->title : "PunPun");
    x_select(g_display,win,(1L<<0)|(1L<<2)|(1L<<3)|(1L<<6)|(1L<<15)|(1L<<17));
    if (g_wm_delete) { Atom protocol = g_wm_delete; x_set_wm_protocols(g_display, win, &protocol, 1); }
    x_flush(g_display); return 1;
}
static void pp_gui_platform_window_destroy(int handle) { if(g_gui_is_headless)return; if(g_display&&g_windows[handle].native)x_destroy(g_display,(Window)g_windows[handle].native); }
static void pp_gui_platform_window_show(int handle,int visible) { if(g_gui_is_headless)return; if(!g_display||!g_windows[handle].native)return; if(visible)x_map(g_display,(Window)g_windows[handle].native);else x_unmap(g_display,(Window)g_windows[handle].native);x_flush(g_display); }
static void pp_gui_platform_window_title(int handle) { if(g_gui_is_headless)return; if(g_display&&g_windows[handle].native)x_store_name(g_display,(Window)g_windows[handle].native,g_windows[handle].title?g_windows[handle].title:"PunPun"); }
static int pp_gui_platform_widget_create(int handle) { (void)handle; return 1; }
static void pp_gui_platform_widget_destroy(int handle) { (void)handle; }
static void pp_gui_platform_widget_sync(int handle) { if(pp_gui_widget_valid(handle))pp_gui_platform_redraw(g_widgets[handle].window); }

static int pp_gui_platform_poll(int window,int64_t timeout_ms) {
    if (g_gui_is_headless) { if (timeout_ms > 0) ppc_plat_sleep_ms(timeout_ms); return 0; }
    if (!g_display) return 0;
    int64_t waited = 0;
    for(;;) {
        while(x_pending(g_display)>0) {
            XEvent ev; x_next(g_display,&ev);
            int owner = pp_gui_find_window_native(ev.xany.window);
            if(owner!=window)continue;
            if (ev.type == PP_X_CLIENT_MESSAGE && (Atom)ev.xclient.data.l[0] == g_wm_delete) {
                g_windows[window].open = 0;
                pp_gui_set_event(PP_GUI_EVENT_CLOSE, window, 0, 0, 0, 0, NULL);
                return g_event.type;
            }
            if(ev.type==PP_X_EXPOSE) { pp_gui_platform_redraw(window); pp_gui_set_event(PP_GUI_EVENT_PAINT,window,0,0,0,0,NULL); return g_event.type; }
            if(ev.type==PP_X_DESTROY_NOTIFY) { g_windows[window].open=0; pp_gui_set_event(PP_GUI_EVENT_CLOSE,window,0,0,0,0,NULL); return g_event.type; }
            if(ev.type==PP_X_CONFIGURE_NOTIFY) { g_windows[window].width=ev.xconfigure.width;g_windows[window].height=ev.xconfigure.height;pp_gui_set_event(PP_GUI_EVENT_RESIZE,window,0,0,ev.xconfigure.width,ev.xconfigure.height,NULL);return g_event.type; }
            if(ev.type==PP_X_MOTION) { int widget=pp_gui_hit_test(window,ev.xbutton.x,ev.xbutton.y); pp_gui_set_event(PP_GUI_EVENT_MOUSE_MOVE,window,widget,0,ev.xbutton.x,ev.xbutton.y,NULL);return g_event.type; }
            if(ev.type==PP_X_BUTTON_PRESS||ev.type==PP_X_BUTTON_RELEASE) {
                int widget=pp_gui_hit_test(window,ev.xbutton.x,ev.xbutton.y);
                if (ev.type == PP_X_BUTTON_PRESS && widget) g_windows[window].focus_widget = widget;
                int event_type=ev.type==PP_X_BUTTON_PRESS?PP_GUI_EVENT_MOUSE_DOWN:PP_GUI_EVENT_MOUSE_UP;
                if(ev.type==PP_X_BUTTON_RELEASE&&widget) {
                    pp_gui_widget_rec *w=&g_widgets[widget];
                    if(w->kind==PP_GUI_BUTTON)event_type=PP_GUI_EVENT_CLICK;
                    else if(w->kind==PP_GUI_CHECKBOX) { w->value=!w->value;event_type=PP_GUI_EVENT_CHANGE;pp_gui_platform_redraw(window); }
                    else if(w->kind==PP_GUI_SLIDER) { int rel=ev.xbutton.x-w->x;if(rel<0)rel=0;if(rel>w->width)rel=w->width;w->value=w->minimum+(w->maximum-w->minimum)*rel/(w->width?w->width:1);event_type=PP_GUI_EVENT_CHANGE;pp_gui_platform_redraw(window); }
                }
                pp_gui_set_event(event_type,window,widget,ev.xbutton.button,ev.xbutton.x,ev.xbutton.y,NULL);return g_event.type;
            }
            if(ev.type==PP_X_KEY_PRESS) {
                char buffer[32]; KeySym sym=0; int n=x_lookup_string(&ev.xkey,buffer,(int)sizeof(buffer)-1,&sym,NULL); if(n<0)n=0;buffer[n]='\0';
                int widget = g_windows[window].focus_widget;
                if (!pp_gui_widget_valid(widget) || g_widgets[widget].window != window) widget = 0;
                if(widget&&g_widgets[widget].kind==PP_GUI_INPUT) {
                    pp_gui_widget_rec *w=&g_widgets[widget]; size_t len=w->text?strlen(w->text):0;
                    if(sym==0xFF08 && len>0) { w->text[len-1]='\0';pp_gui_platform_redraw(window);pp_gui_set_event(PP_GUI_EVENT_TEXT,window,widget,sym,0,0,w->text);return g_event.type; }
                    if(n>0&&len+(size_t)n<PP_GUI_TEXT_CAP) { char *next=(char*)realloc(w->text,len+(size_t)n+1);if(next){w->text=next;memcpy(w->text+len,buffer,(size_t)n+1);pp_gui_platform_redraw(window);pp_gui_set_event(PP_GUI_EVENT_TEXT,window,widget,sym,0,0,w->text);return g_event.type;} }
                }
                pp_gui_set_event(PP_GUI_EVENT_KEY,window,widget,sym,0,0,buffer);return g_event.type;
            }
        }
        if(!g_windows[window].open)return PP_GUI_EVENT_CLOSE;
        if(timeout_ms==0||waited>=timeout_ms)return 0;
        ppc_plat_sleep_ms(1);
        ++waited;
    }
}
static int pp_gui_platform_canvas_clear(int widget,int64_t rgb) { if(g_gui_is_headless)return 1; if(!g_display)return 0;pp_gui_widget_rec*w=&g_widgets[widget];Window win=(Window)g_windows[w->window].native;GC gc=x_gc(g_display,g_screen);x_foreground(g_display,gc,pp_gui_x_color(rgb));x_fill_rect(g_display,win,gc,w->x,w->y,(unsigned)w->width,(unsigned)w->height);x_flush(g_display);return 1; }
static int pp_gui_platform_canvas_rect(int widget,int x,int y,int width,int height,int64_t rgb,int filled) { if(g_gui_is_headless)return 1; if(!g_display)return 0;pp_gui_widget_rec*w=&g_widgets[widget];Window win=(Window)g_windows[w->window].native;GC gc=x_gc(g_display,g_screen);x_foreground(g_display,gc,pp_gui_x_color(rgb));if(filled)x_fill_rect(g_display,win,gc,w->x+x,w->y+y,(unsigned)width,(unsigned)height);else x_draw_rect(g_display,win,gc,w->x+x,w->y+y,(unsigned)width,(unsigned)height);x_flush(g_display);return 1; }
static int pp_gui_platform_canvas_line(int widget,int x1,int y1,int x2,int y2,int64_t rgb) { if(g_gui_is_headless)return 1; if(!g_display)return 0;pp_gui_widget_rec*w=&g_widgets[widget];Window win=(Window)g_windows[w->window].native;GC gc=x_gc(g_display,g_screen);x_foreground(g_display,gc,pp_gui_x_color(rgb));x_draw_line(g_display,win,gc,w->x+x1,w->y+y1,w->x+x2,w->y+y2);x_flush(g_display);return 1; }
static int pp_gui_platform_canvas_text(int widget,int x,int y,const char*text,int64_t rgb) { if(g_gui_is_headless)return 1; if(!g_display)return 0;pp_gui_widget_rec*w=&g_widgets[widget];Window win=(Window)g_windows[w->window].native;GC gc=x_gc(g_display,g_screen);x_foreground(g_display,gc,pp_gui_x_color(rgb));const char*s=text?text:"";x_draw_string(g_display,win,gc,w->x+x,w->y+y,s,(int)strlen(s));x_flush(g_display);return 1; }

bool pp_gui_message(const char *title,const char *message) {
    if (!pp_gui_available()) return false;
    int64_t window = pp_gui_window_create(title ? title : "PunPun", 560, 180);
    if (!window) return false;
    int64_t label=pp_gui_widget_create(window,PP_GUI_LABEL,message?message:"");pp_gui_widget_set_bounds(label,24,36,512,40);
    int64_t button=pp_gui_widget_create(window,PP_GUI_BUTTON,"OK");pp_gui_widget_set_bounds(button,230,110,100,32);pp_gui_window_show(window,true);
    while(pp_gui_window_open(window)){int64_t event=pp_gui_poll(window,50);if(event==PP_GUI_EVENT_CLICK&&pp_gui_event_widget()==button)break;if(event==PP_GUI_EVENT_CLOSE)break;}
    pp_gui_window_close(window);return true;
}
#endif
