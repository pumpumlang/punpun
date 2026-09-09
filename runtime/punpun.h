#ifndef PUNPUN_RUNTIME_H
#define PUNPUN_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#define PUNPUN_RUNTIME_ABI_VERSION 1

typedef struct pp_numbers pp_numbers;
typedef struct pp_i64_slice pp_i64_slice;
typedef struct pp_task pp_task;
typedef struct pp_task_group pp_task_group;
typedef uintptr_t (*pp_task_entry)(void *context);

/* Runtime-owned strings and lists live until cleanup; there is no incremental
 * GC. Do not free returned pointers. Init registers cleanup with atexit and
 * borrows argv until cleanup. Cleanup is idempotent and invalidates all owned
 * pointers. Panics abort, so they do not run atexit handlers. */
int pp_runtime_abi_version(void);
void pp_runtime_init(int argc, char **argv);
void pp_runtime_cleanup(void);
void *pp_object_alloc(int64_t size);
void pp_object_free(void *pointer);
int64_t pp_arg_count(void);
const char *pp_arg(int64_t index);

pp_numbers *pp_numbers_new(void);
void pp_numbers_free(pp_numbers *numbers);
void pp_push(pp_numbers *numbers, int64_t value);
int64_t pp_at(pp_numbers *numbers, int64_t index);
void pp_put(pp_numbers *numbers, int64_t index, int64_t value);
int64_t pp_size(pp_numbers *numbers);
int64_t pp_pop(pp_numbers *numbers);
void pp_sort(pp_numbers *numbers);
pp_i64_slice *pp_numbers_view(pp_numbers *numbers, int64_t start, int64_t end);
int64_t pp_slice_len_i64(pp_i64_slice *slice);
int64_t pp_slice_at_i64(pp_i64_slice *slice, int64_t index);

/* Text inputs must be non-NULL, NUL-terminated strings. Bounds are zero-based
 * byte offsets; slice excludes end. read_text rejects embedded NUL bytes. */
/* Adopt a malloc-compatible NUL-terminated string returned by native/FFI code.
 * The PunPun runtime owns and frees it during cleanup. */
const char *pp_adopt_text(char *owned_text);
const char *pp_concat(const char *left, const char *right);
const char *pp_slice(const char *text, int64_t start, int64_t end);
bool pp_contains(const char *text, const char *part);
const char *pp_read_text(const char *path);
void pp_write_text(const char *path, const char *text);
bool pp_file_exists(const char *path);
bool pp_make_dir(const char *path);
bool pp_remove_file(const char *path);
bool pp_rename_file(const char *from, const char *to);
const char *pp_path_join(const char *left, const char *right);
const char *pp_current_dir(void);
bool pp_env_has(const char *name);
const char *pp_env_or(const char *name, const char *fallback);
const char *pp_platform(void);
const char *pp_read_line(void);
void pp_sleep_ms(int64_t duration);
bool pp_utf8_valid(const char *text);
int64_t pp_utf8_len(const char *text);

/* Async task runtime. The runtime copies the argument/context block before
 * starting the worker, so generated code may pass stack-backed call frames.
 * Await joins the native worker exactly once; repeated awaits return the same
 * completed result. Ordinary programs that never call pp_task_spawn do not
 * create any threads. */
pp_task *pp_task_spawn(pp_task_entry entry, const void *context, int64_t context_size);
void pp_task_cancel(pp_task *task);
bool pp_task_is_done(pp_task *task);
bool pp_task_cancelled(void);
uintptr_t pp_task_await_bits(pp_task *task);
int64_t pp_task_await_i64(pp_task *task);
double pp_task_await_f64(pp_task *task);
void *pp_task_await_ptr(pp_task *task);
void pp_task_await_void(pp_task *task);

/* Structured task groups. Handles are opaque positive runtime IDs represented
 * as i64 in the language ABI. Groups do not steal task ownership: the runtime
 * still owns each task until cleanup, while a group owns only its membership
 * list. Waiting is repeatable and cancellation is cooperative. */
int64_t pp_task_group_new(void);
void pp_task_group_add(int64_t group_handle, pp_task *task);
void pp_task_group_cancel(int64_t group_handle);
void pp_task_group_wait(int64_t group_handle);
bool pp_task_group_wait_for(int64_t group_handle, int64_t timeout_ms);
bool pp_task_group_is_done(int64_t group_handle);
int64_t pp_task_group_pending(int64_t group_handle);
void pp_task_group_close(int64_t group_handle);
const char *pp_text_int(int64_t value);
/* Parse an optional sign followed by ASCII decimal digits, without whitespace.
 * whole truncates toward zero after rejecting nonfinite/out-of-range values. */
int64_t pp_parse_int(const char *text);
double pp_decimal(int64_t value);
int64_t pp_whole(double value);
void pp_assert(bool condition, const char *message);

void pp_print_int(int64_t value);
void pp_print_float(double value);
void pp_print_bool(bool value);
void pp_print_str(const char *value);
void pp_println_int(int64_t value);
void pp_println_float(double value);
void pp_println_bool(bool value);
void pp_println_str(const char *value);
int64_t pp_len(const char *value);
bool pp_str_eq(const char *left, const char *right);
void pp_panic(const char *message);
int64_t pp_clock_ms(void);
int64_t pp_abs_i64(int64_t value);
int64_t pp_add_i64(int64_t left, int64_t right);
int64_t pp_sub_i64(int64_t left, int64_t right);
int64_t pp_mul_i64(int64_t left, int64_t right);
int64_t pp_div_i64(int64_t left, int64_t right);
int64_t pp_mod_i64(int64_t left, int64_t right);
int64_t pp_neg_i64(int64_t value);

#endif
