/* ppc runtime library.
 *
 * This deliberately exports the same `pp_*` symbols as the PunPun reference
 * runtime, with the same signatures and the same ownership rules. Code emitted
 * by ppc therefore links against either runtime, which is what makes it
 * possible to compare the two compilers' output on identical programs.
 *
 * Memory model: strings and lists returned by the runtime are owned by the
 * runtime and live until cleanup. Callers never free them. Cleanup is
 * registered with atexit by pp_runtime_init, is idempotent, and invalidates
 * every pointer the runtime handed out. Panics abort, so they do not run atexit
 * handlers.
 */
#ifndef PPC_RUNTIME_H
#define PPC_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime ABI epoch. Generated code checks this so a program built against one
 * runtime cannot silently link against an incompatible one. */
#define PUNPUN_RUNTIME_ABI_VERSION 1
int32_t pp_runtime_abi_version(void);

typedef struct pp_numbers pp_numbers;
typedef struct pp_list pp_list;
typedef struct pp_map pp_map;
typedef struct pp_bytes pp_bytes;
typedef struct pp_i64_slice pp_i64_slice;
typedef struct pp_task pp_task;
typedef uintptr_t pp_closure;
typedef uintptr_t (*pp_task_entry)(void *context);

/* -- lifecycle ---------------------------------------------------------- */
void pp_runtime_init(int argc, char **argv);
void pp_runtime_cleanup(void);
void *pp_object_alloc(int64_t size);
void pp_object_free(void *pointer);

/* -- closures ----------------------------------------------------------- */
/* A closure handle is one word. Bit 0 set means a zero-capture function and
 * the remaining bits are its target index; aligned heap environment pointers
 * have bit 0 clear. That keeps ordinary function values allocation-free. */
static inline pp_closure pp_closure_named(int64_t target) {
    return (((pp_closure)(uint64_t)target) << 1) | (pp_closure)1;
}
pp_closure pp_closure_new(int64_t target, int64_t capture_count);
int64_t pp_closure_target(pp_closure closure);
int64_t pp_closure_get(pp_closure closure, int64_t index);
void pp_closure_set(pp_closure closure, int64_t index, int64_t slot);

/* -- secure HTTPS -------------------------------------------------------
 * The implementation loads the system libcurl runtime dynamically. Only
 * https:// URLs are accepted; peer/host verification cannot be disabled.
 * Headers are separated by newlines. Status and error describe the calling
 * thread's most recent request.
 */
void pp_https_runtime_init(void);
bool pp_https_available(void);
const char *pp_https_request(const char *method, const char *url, const char *body,
                             const char *headers, int64_t timeout_ms,
                             bool follow_redirects);
pp_bytes *pp_https_request_bytes(const char *method, const char *url, pp_bytes *body,
                                 const char *headers, int64_t timeout_ms,
                                 bool follow_redirects);
int64_t pp_https_status(void);
const char *pp_https_error(void);
const char *pp_https_headers_raw(void);
pp_bytes *pp_https_body_bytes(void);

/* -- sockets / DNS ------------------------------------------------------
 * Handles are small positive integers owned by the runtime, never raw OS
 * descriptors. Sockets are nonblocking internally; timeout waits are
 * cancellation-aware and set pp_net_timed_out() instead of blocking forever.
 */
void pp_net_runtime_init(void);
void pp_net_runtime_cleanup(void);
bool pp_net_available(void);
const char *pp_net_error(void);
bool pp_net_timed_out(void);
bool pp_net_eof(void);
const char *pp_net_last_host(void);
int64_t pp_net_last_port(void);
pp_list *pp_net_resolve(const char *host, int64_t family);
int64_t pp_net_tcp_connect(const char *host, int64_t port, int64_t timeout_ms);
int64_t pp_net_tcp_listen(const char *host, int64_t port, int64_t backlog);
int64_t pp_net_tcp_accept(int64_t listener, int64_t timeout_ms);
int64_t pp_net_udp_bind(const char *host, int64_t port);
bool pp_net_socket_close(int64_t handle);
bool pp_net_socket_shutdown(int64_t handle, int64_t how);
bool pp_net_socket_wait_readable(int64_t handle, int64_t timeout_ms);
bool pp_net_socket_wait_writable(int64_t handle, int64_t timeout_ms);
bool pp_net_socket_set_nodelay(int64_t handle, bool enabled);
int64_t pp_net_socket_send(int64_t handle, pp_bytes *bytes, int64_t timeout_ms);
pp_bytes *pp_net_socket_recv(int64_t handle, int64_t max_bytes, int64_t timeout_ms);
int64_t pp_net_udp_send_to(int64_t handle, const char *host, int64_t port,
                           pp_bytes *bytes, int64_t timeout_ms);
pp_bytes *pp_net_udp_recv_from(int64_t handle, int64_t max_bytes, int64_t timeout_ms);
int64_t pp_net_socket_local_port(int64_t handle);
const char *pp_net_socket_peer_host(int64_t handle);
int64_t pp_net_socket_peer_port(int64_t handle);

/* -- native GUI ---------------------------------------------------------
 * Windows uses Win32; POSIX uses a dynamically loaded X11/XWayland runtime.
 * Availability is false on headless hosts and when no supported display is
 * present.
 */
void pp_gui_runtime_init(void);
void pp_gui_runtime_cleanup(void);
bool pp_gui_available(void);
bool pp_gui_headless(void);
bool pp_gui_message(const char *title, const char *message);
int64_t pp_gui_window_create(const char *title, int64_t width, int64_t height);
bool pp_gui_window_show(int64_t handle, bool visible);
bool pp_gui_window_close(int64_t handle);
bool pp_gui_window_open(int64_t handle);
bool pp_gui_window_set_title(int64_t handle, const char *title);
int64_t pp_gui_window_width(int64_t handle);
int64_t pp_gui_window_height(int64_t handle);
int64_t pp_gui_widget_create(int64_t window, int64_t kind, const char *text);
bool pp_gui_widget_destroy(int64_t handle);
bool pp_gui_widget_set_bounds(int64_t handle, int64_t x, int64_t y, int64_t width, int64_t height);
int64_t pp_gui_widget_x(int64_t handle);
int64_t pp_gui_widget_y(int64_t handle);
int64_t pp_gui_widget_width(int64_t handle);
int64_t pp_gui_widget_height(int64_t handle);
bool pp_gui_widget_set_text(int64_t handle, const char *text);
const char *pp_gui_widget_text(int64_t handle);
bool pp_gui_widget_set_value(int64_t handle, int64_t value);
int64_t pp_gui_widget_value(int64_t handle);
bool pp_gui_widget_set_range(int64_t handle, int64_t minimum, int64_t maximum);
bool pp_gui_widget_set_visible(int64_t handle, bool visible);
bool pp_gui_widget_set_enabled(int64_t handle, bool enabled);
bool pp_gui_redraw(int64_t window);
int64_t pp_gui_poll(int64_t window, int64_t timeout_ms);
bool pp_gui_post_event(int64_t window, int64_t type, int64_t widget, int64_t key, const char *text, int64_t x, int64_t y);
int64_t pp_gui_event_window(void);
int64_t pp_gui_event_widget(void);
int64_t pp_gui_event_key(void);
int64_t pp_gui_event_x(void);
int64_t pp_gui_event_y(void);
const char *pp_gui_event_text(void);
bool pp_gui_canvas_clear(int64_t widget, int64_t rgb);
bool pp_gui_canvas_rect(int64_t widget, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgb, bool filled);
bool pp_gui_canvas_line(int64_t widget, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t rgb);
bool pp_gui_canvas_text(int64_t widget, int64_t x, int64_t y, const char *text, int64_t rgb);

/* -- program arguments -------------------------------------------------- */
int64_t pp_arg_count(void);
const char *pp_arg(int64_t index);

/* -- nums: a growable list of int64 with checked access ------------------ */
pp_numbers *pp_numbers_new(void);
void pp_numbers_free(pp_numbers *numbers);
void pp_push(pp_numbers *numbers, int64_t value);
int64_t pp_at(pp_numbers *numbers, int64_t index);
void pp_put(pp_numbers *numbers, int64_t index, int64_t value);
int64_t pp_size(pp_numbers *numbers);
int64_t pp_pop(pp_numbers *numbers);
void pp_sort(pp_numbers *numbers);

/* -- List<T>: a growable sequence of any element type ---------------------
 *
 * Elements are 8-byte slots. Every PunPun value the backends can put in a
 * variable already fits one: an int, a float's bits, a str pointer, or a handle
 * to a boxed aggregate. The compiler knows the element type statically and
 * checks it, so the runtime does not need to and stores raw slots.
 *
 * This is why one runtime implementation serves List<int>, List<str>, and
 * List<SomeStruct> without templates or per-type code.
 */
/* Reinterpret a double as slot bits and back. A List stores raw slots, so a
 * float element must round-trip its bits; converting the value would round. */
int64_t pp_bits_from_f64(double value);
double pp_f64_from_bits(int64_t bits);

pp_list *pp_list_new(void);
void pp_list_push(pp_list *list, int64_t slot);
int64_t pp_list_at(pp_list *list, int64_t index);
void pp_list_put(pp_list *list, int64_t index, int64_t slot);
int64_t pp_list_size(pp_list *list);
int64_t pp_list_pop(pp_list *list);
void pp_list_clear(pp_list *list);

/* -- Map<V>: a string-keyed hash map --------------------------------------
 *
 * Keys are always str; values are 8-byte slots, as in pp_list. String keys
 * cover the overwhelming majority of what a program needs a map for — symbol
 * tables, headers, configuration, JSON objects — and restricting to them keeps
 * one implementation instead of one per key type.
 *
 * The map copies each key it stores, so a caller may free or reuse the string
 * it passed in.
 */
pp_map *pp_map_new(void);
void pp_map_put(pp_map *map, const char *key, int64_t slot);
int64_t pp_map_get(pp_map *map, const char *key);
int64_t pp_map_get_or(pp_map *map, const char *key, int64_t fallback);
bool pp_map_has(pp_map *map, const char *key);
bool pp_map_remove(pp_map *map, const char *key);
int64_t pp_map_size(pp_map *map);
pp_list *pp_map_keys(pp_map *map);
void pp_map_clear(pp_map *map);

/* -- bytes: a mutable byte buffer ------------------------------------------
 *
 * PunPun `str` is UTF-8 text and carries no NUL, so it cannot represent
 * arbitrary binary. Anything that reads a file, hashes, encodes, or talks to a
 * socket needs this instead.
 */
pp_bytes *pp_bytes_new(void);
pp_bytes *pp_bytes_from_text(const char *text);
const char *pp_bytes_to_text(pp_bytes *bytes);
void pp_bytes_push(pp_bytes *bytes, int64_t value);
int64_t pp_bytes_at(pp_bytes *bytes, int64_t index);
void pp_bytes_put(pp_bytes *bytes, int64_t index, int64_t value);
int64_t pp_bytes_len(pp_bytes *bytes);
pp_bytes *pp_bytes_slice(pp_bytes *bytes, int64_t start, int64_t end);
pp_bytes *pp_bytes_concat(pp_bytes *left, pp_bytes *right);
/* Runtime-internal binary access used by socket and crypto modules. */
const uint8_t *pp_bytes_data(pp_bytes *bytes);
pp_bytes *pp_bytes_from_data(const void *data, int64_t length);

/* -- checked borrowed views --------------------------------------------- */
pp_i64_slice *pp_numbers_view(pp_numbers *numbers, int64_t start, int64_t end);
int64_t pp_slice_len_i64(pp_i64_slice *slice);
int64_t pp_slice_at_i64(pp_i64_slice *slice, int64_t index);

/* -- text ---------------------------------------------------------------- */
const char *pp_adopt_text(char *owned_text);
const char *pp_concat(const char *left, const char *right);
const char *pp_slice(const char *text, int64_t start, int64_t end);
bool pp_contains(const char *text, const char *part);
int64_t pp_len(const char *value);
bool pp_str_eq(const char *left, const char *right);
int64_t pp_str_cmp(const char *left, const char *right);
const char *pp_text_int(int64_t value);
int64_t pp_parse_int(const char *text);
double pp_decimal(int64_t value);
int64_t pp_whole(double value);
bool pp_utf8_valid(const char *text);
int64_t pp_utf8_len(const char *text);

/* -- text operations -------------------------------------------------------
 * The building blocks every text library needs. Indices are byte offsets, not
 * scalar values: PunPun str is UTF-8, and a library that needs scalar-level
 * work builds it from these.
 */
int64_t pp_char_at(const char *text, int64_t index);
const char *pp_char_str(int64_t code);
int64_t pp_index_of(const char *text, const char *part, int64_t from);
int64_t pp_last_index_of(const char *text, const char *part);
bool pp_starts_with(const char *text, const char *prefix);
bool pp_ends_with(const char *text, const char *suffix);
const char *pp_to_upper(const char *text);
const char *pp_to_lower(const char *text);
const char *pp_trim(const char *text);
const char *pp_replace(const char *text, const char *from, const char *to);
const char *pp_repeat(const char *text, int64_t times);
pp_list *pp_split(const char *text, const char *separator);
const char *pp_join(pp_list *parts, const char *separator);
const char *pp_text_float(double value);
double pp_parse_float(const char *text);
const char *pp_pad_left(const char *text, int64_t width, const char *fill);
const char *pp_pad_right(const char *text, int64_t width, const char *fill);

/* -- math ------------------------------------------------------------------
 * Thin wrappers over libm. They exist so PunPun code has one spelling that is
 * identical on every backend, rather than each backend inventing its own.
 */
double pp_sqrt(double value);
double pp_pow(double base, double exponent);
double pp_exp(double value);
double pp_log(double value);
double pp_log2(double value);
double pp_log10(double value);
double pp_sin(double value);
double pp_cos(double value);
double pp_tan(double value);
double pp_asin(double value);
double pp_acos(double value);
double pp_atan(double value);
double pp_atan2(double y, double x);
double pp_floor(double value);
double pp_ceil(double value);
double pp_round(double value);
double pp_fabs(double value);
double pp_fmod(double left, double right);
double pp_hypot(double x, double y);
bool pp_is_nan(double value);
bool pp_is_infinite(double value);

/* -- pseudorandom ----------------------------------------------------------
 * A seeded xorshift generator. Deterministic for a given seed, which is what a
 * test or a simulation needs. NOT suitable for anything security-related; use
 * pp_random_bytes for that.
 */
void pp_random_seed(int64_t seed);
int64_t pp_random_int(int64_t low, int64_t high);
double pp_random_float(void);
/* Cryptographically secure bytes from the operating system. */
pp_bytes *pp_random_bytes(int64_t count);

/* -- filesystem ---------------------------------------------------------- */
const char *pp_read_text(const char *path);
void pp_write_text(const char *path, const char *text);
bool pp_file_exists(const char *path);
bool pp_make_dir(const char *path);
bool pp_remove_file(const char *path);
bool pp_rename_file(const char *from, const char *to);
const char *pp_path_join(const char *left, const char *right);
pp_list *pp_list_dir(const char *path);
bool pp_is_dir(const char *path);
int64_t pp_file_size_of(const char *path);
pp_bytes *pp_read_bytes(const char *path);
void pp_write_bytes(const char *path, pp_bytes *bytes);
void pp_append_text(const char *path, const char *text);
bool pp_remove_dir(const char *path);
bool pp_make_dirs(const char *path);
const char *pp_path_parent(const char *path);
const char *pp_path_name(const char *path);
const char *pp_path_extension(const char *path);

/* -- time ------------------------------------------------------------------ */
int64_t pp_now_ms(void);
int64_t pp_year_of(int64_t epoch_ms);
int64_t pp_month_of(int64_t epoch_ms);
int64_t pp_day_of(int64_t epoch_ms);
int64_t pp_hour_of(int64_t epoch_ms);
int64_t pp_minute_of(int64_t epoch_ms);
int64_t pp_second_of(int64_t epoch_ms);
int64_t pp_weekday_of(int64_t epoch_ms);

/* -- process --------------------------------------------------------------- */
void pp_exit(int64_t status);
int64_t pp_run_command(const char *command);
const char *pp_current_dir(void);

/* -- environment and process --------------------------------------------- */
bool pp_env_has(const char *name);
const char *pp_env_or(const char *name, const char *fallback);
bool pp_env_set(const char *name, const char *value);
const char *pp_hostname(void);
int64_t pp_cpu_count(void);
const char *pp_process_capture(const char *command);
int64_t pp_process_status(void);
const char *pp_platform(void);
const char *pp_read_line(void);
void pp_sleep_ms(int64_t duration);
int64_t pp_clock_ms(void);

/* -- console -------------------------------------------------------------- */
void pp_print_int(int64_t value);
void pp_print_float(double value);
void pp_print_bool(bool value);
void pp_print_str(const char *value);
void pp_println_int(int64_t value);
void pp_println_float(double value);
void pp_println_bool(bool value);
void pp_println_str(const char *value);

/* -- diagnostics ---------------------------------------------------------- */
void pp_panic(const char *message);
void pp_assert(bool condition, const char *message);

/* -- checked arithmetic ---------------------------------------------------
 * Integer arithmetic in PunPun traps on overflow rather than wrapping. These
 * helpers are what the backends emit for `-O0` and for any operation the
 * optimizer could not prove safe.
 */
int64_t pp_add_i64(int64_t left, int64_t right);
int64_t pp_sub_i64(int64_t left, int64_t right);
int64_t pp_mul_i64(int64_t left, int64_t right);
int64_t pp_div_i64(int64_t left, int64_t right);
int64_t pp_mod_i64(int64_t left, int64_t right);
int64_t pp_neg_i64(int64_t value);
int64_t pp_abs_i64(int64_t value);
int64_t pp_shl_i64(int64_t value, int64_t amount);
int64_t pp_shr_i64(int64_t value, int64_t amount);

/* -- async tasks ---------------------------------------------------------- */
pp_task *pp_task_spawn(pp_task_entry entry, const void *context, int64_t context_size);
void pp_task_cancel(pp_task *task);
bool pp_task_is_done(pp_task *task);
bool pp_task_cancelled(void);
uintptr_t pp_task_await_bits(pp_task *task);
int64_t pp_task_await_i64(pp_task *task);
double pp_task_await_f64(pp_task *task);
void *pp_task_await_ptr(pp_task *task);
void pp_task_await_void(pp_task *task);

/* -- structured task groups ----------------------------------------------
 * A group is referenced by an opaque integer handle rather than a pointer, so
 * it survives the FFI boundary and can be stored in an ordinary `int`.
 * Cancellation is cooperative throughout: a group never kills native code, it
 * only asks, and workers observe the request at safe points such as sleep_ms.
 */
int64_t pp_task_group_new(void);
void pp_task_group_add(int64_t group_handle, pp_task *task);
void pp_task_group_cancel(int64_t group_handle);
void pp_task_group_wait(int64_t group_handle);
bool pp_task_group_wait_for(int64_t group_handle, int64_t timeout_ms);
bool pp_task_group_is_done(int64_t group_handle);
int64_t pp_task_group_pending(int64_t group_handle);
void pp_task_group_close(int64_t group_handle);

#ifdef __cplusplus
}
#endif

#endif
