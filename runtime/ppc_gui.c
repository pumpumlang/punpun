/* Minimal cross-platform native GUI foundation for PunPun.
 *
 * Windows uses the Win32 message box API. POSIX loads X11 dynamically, which
 * keeps PPC build-time dependency free and makes availability explicit on
 * headless systems and Wayland sessions without XWayland.
 */
#include "ppcrt.h"
#include "ppc_platform.h"

#include <stdint.h>
#include <string.h>

#if PPC_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

void pp_gui_runtime_init(void) {}
bool pp_gui_available(void) { return true; }
bool pp_gui_message(const char *title, const char *message) {
    return MessageBoxA(NULL, message ? message : "", title ? title : "PunPun",
                       MB_OK | MB_ICONINFORMATION) == IDOK;
}

#else
#  include <dlfcn.h>

typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef struct _XGC *GC;
typedef union { int type; long padding[24]; } XEvent;

enum {
    PP_X_KEY_PRESS = 2,
    PP_X_BUTTON_PRESS = 4,
    PP_X_EXPOSE = 12,
    PP_X_DESTROY_NOTIFY = 17
};

typedef Display *(*pp_x_open_fn)(const char *);
typedef int (*pp_x_close_fn)(Display *);
typedef int (*pp_x_default_screen_fn)(Display *);
typedef Window (*pp_x_root_window_fn)(Display *, int);
typedef unsigned long (*pp_x_pixel_fn)(Display *, int);
typedef Window (*pp_x_create_window_fn)(Display *, Window, int, int, unsigned int,
                                         unsigned int, unsigned int, unsigned long,
                                         unsigned long);
typedef int (*pp_x_store_name_fn)(Display *, Window, const char *);
typedef int (*pp_x_select_input_fn)(Display *, Window, long);
typedef int (*pp_x_map_window_fn)(Display *, Window);
typedef int (*pp_x_next_event_fn)(Display *, XEvent *);
typedef int (*pp_x_destroy_window_fn)(Display *, Window);
typedef int (*pp_x_flush_fn)(Display *);
typedef GC (*pp_x_default_gc_fn)(Display *, int);
typedef int (*pp_x_set_foreground_fn)(Display *, GC, unsigned long);
typedef int (*pp_x_draw_string_fn)(Display *, Window, GC, int, int, const char *, int);
typedef int (*pp_x_init_threads_fn)(void);

static void *g_x11;
static pp_x_open_fn g_x_open;
static pp_x_close_fn g_x_close;
static pp_x_default_screen_fn g_x_screen;
static pp_x_root_window_fn g_x_root;
static pp_x_pixel_fn g_x_black;
static pp_x_pixel_fn g_x_white;
static pp_x_create_window_fn g_x_create;
static pp_x_store_name_fn g_x_store_name;
static pp_x_select_input_fn g_x_select_input;
static pp_x_map_window_fn g_x_map;
static pp_x_next_event_fn g_x_next;
static pp_x_destroy_window_fn g_x_destroy;
static pp_x_flush_fn g_x_flush;
static pp_x_default_gc_fn g_x_gc;
static pp_x_set_foreground_fn g_x_foreground;
static pp_x_draw_string_fn g_x_draw;
static int g_gui_initialized;
static int g_gui_loaded;

static int pp_gui_bind(void **slot, const char *name) {
    *slot = dlsym(g_x11, name);
    return *slot != NULL;
}

void pp_gui_runtime_init(void) {
    if (g_gui_initialized) return;
    g_gui_initialized = 1;
    g_x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!g_x11) g_x11 = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_x11) return;
#define PP_X_BIND(target, name) \
    do { if (!pp_gui_bind((void **)&(target), (name))) return; } while (0)
    PP_X_BIND(g_x_open, "XOpenDisplay");
    PP_X_BIND(g_x_close, "XCloseDisplay");
    PP_X_BIND(g_x_screen, "XDefaultScreen");
    PP_X_BIND(g_x_root, "XRootWindow");
    PP_X_BIND(g_x_black, "XBlackPixel");
    PP_X_BIND(g_x_white, "XWhitePixel");
    PP_X_BIND(g_x_create, "XCreateSimpleWindow");
    PP_X_BIND(g_x_store_name, "XStoreName");
    PP_X_BIND(g_x_select_input, "XSelectInput");
    PP_X_BIND(g_x_map, "XMapWindow");
    PP_X_BIND(g_x_next, "XNextEvent");
    PP_X_BIND(g_x_destroy, "XDestroyWindow");
    PP_X_BIND(g_x_flush, "XFlush");
    PP_X_BIND(g_x_gc, "XDefaultGC");
    PP_X_BIND(g_x_foreground, "XSetForeground");
    PP_X_BIND(g_x_draw, "XDrawString");
#undef PP_X_BIND
    pp_x_init_threads_fn init_threads = NULL;
    (void)pp_gui_bind((void **)&init_threads, "XInitThreads");
    if (init_threads) (void)init_threads();
    g_gui_loaded = 1;
}

bool pp_gui_available(void) {
    if (!g_gui_initialized) pp_gui_runtime_init();
    if (!g_gui_loaded) return false;
    Display *display = g_x_open(NULL);
    if (!display) return false;
    g_x_close(display);
    return true;
}

bool pp_gui_message(const char *title, const char *message) {
    if (!g_gui_initialized) pp_gui_runtime_init();
    if (!g_gui_loaded) return false;
    Display *display = g_x_open(NULL);
    if (!display) return false;

    const int screen = g_x_screen(display);
    const unsigned long black = g_x_black(display, screen);
    const unsigned long white = g_x_white(display, screen);
    Window window = g_x_create(display, g_x_root(display, screen), 80, 80,
                               560, 180, 1, black, white);
    g_x_store_name(display, window, title ? title : "PunPun");
    g_x_select_input(display, window,
                     (1L << 0) | (1L << 2) | (1L << 15) | (1L << 17));
    g_x_map(display, window);
    g_x_flush(display);

    const char *shown = message ? message : "";
    const int shown_length = (int)strlen(shown);
    int open = 1;
    int destroyed = 0;
    while (open) {
        XEvent event;
        g_x_next(display, &event);
        if (event.type == PP_X_EXPOSE) {
            GC gc = g_x_gc(display, screen);
            g_x_foreground(display, gc, black);
            g_x_draw(display, window, gc, 24, 72, shown, shown_length);
            g_x_draw(display, window, gc, 24, 132,
                     "Click the window or press a key to close.", 40);
            g_x_flush(display);
        } else if (event.type == PP_X_KEY_PRESS || event.type == PP_X_BUTTON_PRESS ||
                   event.type == PP_X_DESTROY_NOTIFY) {
            destroyed = event.type == PP_X_DESTROY_NOTIFY;
            open = 0;
        }
    }
    if (!destroyed) g_x_destroy(display, window);
    g_x_close(display);
    return true;
}

#endif
