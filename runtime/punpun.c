#define _POSIX_C_SOURCE 200809L
#include "punpun.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

struct pp_numbers {
    int64_t *values;
    size_t length;
    size_t capacity;
    struct pp_numbers *next;
};

struct pp_i64_slice {
    pp_numbers *owner;
    size_t start;
    size_t length;
    struct pp_i64_slice *next;
};

struct owned_text {
    char *value;
    struct owned_text *next;
};

struct owned_object {
    void *value;
    struct owned_object *next;
};

struct pp_task {
    pp_task_entry entry;
    void *context;
    uintptr_t result;
    bool joined;
    atomic_bool cancel_requested;
    atomic_bool finished;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    struct pp_task *next;
};

static pp_numbers *owned_numbers;
static pp_i64_slice *owned_slices;
static struct owned_text *owned_strings;
static struct owned_object *owned_objects;
static pp_task *owned_tasks;
static atomic_flag registry_lock = ATOMIC_FLAG_INIT;
static bool cleanup_registered;
static int runtime_argc;
static char **runtime_argv;
#ifdef _MSC_VER
static __declspec(thread) pp_task *current_task;
#else
static _Thread_local pp_task *current_task;
#endif

static void pp_lock_registry(void) {
    while (atomic_flag_test_and_set_explicit(&registry_lock, memory_order_acquire)) {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
    }
}

static void pp_unlock_registry(void) {
    atomic_flag_clear_explicit(&registry_lock, memory_order_release);
}

#ifdef _WIN32
static DWORD WINAPI pp_task_worker(LPVOID raw) {
    pp_task *task = (pp_task *)raw;
    current_task = task;
    task->result = task->entry(task->context);
    free(task->context);
    task->context = NULL;
    atomic_store_explicit(&task->finished, true, memory_order_release);
    current_task = NULL;
    return 0;
}
#else
static void *pp_task_worker(void *raw) {
    pp_task *task = (pp_task *)raw;
    current_task = task;
    task->result = task->entry(task->context);
    free(task->context);
    task->context = NULL;
    atomic_store_explicit(&task->finished, true, memory_order_release);
    current_task = NULL;
    return NULL;
}
#endif

static void pp_task_join(pp_task *task) {
    if (task == NULL) pp_panic("cannot await null task");
    if (task->joined) return;
#ifdef _WIN32
    if (WaitForSingleObject(task->thread, INFINITE) != WAIT_OBJECT_0) pp_panic("cannot join async task");
    CloseHandle(task->thread);
    task->thread = NULL;
#else
    if (pthread_join(task->thread, NULL) != 0) pp_panic("cannot join async task");
#endif
    task->joined = true;
}

void pp_runtime_init(int argc, char **argv) {
    if (argc < 0 || (argc > 0 && argv == NULL)) pp_panic("invalid runtime arguments");
    if (!cleanup_registered) {
        if (atexit(pp_runtime_cleanup) != 0) pp_panic("cannot register runtime cleanup");
        cleanup_registered = true;
    }
    runtime_argc = argc;
    runtime_argv = argv;
}

void pp_runtime_cleanup(void) {
    for (;;) {
        pp_lock_registry();
        pp_task *task = owned_tasks;
        if (task != NULL) owned_tasks = task->next;
        pp_unlock_registry();
        if (task == NULL) break;
        pp_task_join(task);
        free(task);
    }
    while (owned_slices != NULL) {
        pp_i64_slice *next = owned_slices->next;
        free(owned_slices);
        owned_slices = next;
    }
    while (owned_numbers != NULL) {
        pp_numbers *next = owned_numbers->next;
        free(owned_numbers->values);
        free(owned_numbers);
        owned_numbers = next;
    }
    while (owned_strings != NULL) {
        struct owned_text *next = owned_strings->next;
        free(owned_strings->value);
        free(owned_strings);
        owned_strings = next;
    }
    while (owned_objects != NULL) {
        struct owned_object *next = owned_objects->next;
        free(owned_objects->value);
        free(owned_objects);
        owned_objects = next;
    }
    runtime_argc = 0;
    runtime_argv = NULL;
}

void *pp_object_alloc(int64_t size) {
    if (size <= 0 || (uintmax_t)size > (uintmax_t)SIZE_MAX) pp_panic("invalid object allocation size");
    void *value = calloc(1, (size_t)size);
    if (value == NULL) pp_panic("out of memory");
    struct owned_object *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(value);
        pp_panic("out of memory");
    }
    pp_lock_registry();
    *entry = (struct owned_object){.value = value, .next = owned_objects};
    owned_objects = entry;
    pp_unlock_registry();
    return value;
}

void pp_object_free(void *pointer) {
    if (pointer == NULL) return;
    pp_lock_registry();
    struct owned_object **slot = &owned_objects;
    while (*slot != NULL) {
        if ((*slot)->value == pointer) {
            struct owned_object *entry = *slot;
            *slot = entry->next;
            pp_unlock_registry();
            free(entry->value);
            free(entry);
            return;
        }
        slot = &(*slot)->next;
    }
    pp_unlock_registry();
    pp_panic("attempt to free an unknown or already-freed object");
}

int64_t pp_arg_count(void) { return runtime_argc > 0 ? (int64_t)runtime_argc - 1 : 0; }

const char *pp_arg(int64_t index) {
    if (index < 0 || index >= pp_arg_count()) pp_panic("argument index out of bounds");
    return runtime_argv[(size_t)index + 1];
}

pp_numbers *pp_numbers_new(void) {
    pp_numbers *numbers = malloc(sizeof(*numbers));
    if (numbers == NULL) pp_panic("out of memory");
    pp_lock_registry();
    *numbers = (pp_numbers){.next = owned_numbers};
    owned_numbers = numbers;
    pp_unlock_registry();
    return numbers;
}

void pp_numbers_free(pp_numbers *numbers) {
    if (numbers == NULL) return;
    pp_lock_registry();
    pp_numbers **slot = &owned_numbers;
    while (*slot != NULL) {
        if (*slot == numbers) {
            *slot = numbers->next;
            pp_unlock_registry();
            free(numbers->values);
            free(numbers);
            return;
        }
        slot = &(*slot)->next;
    }
    pp_unlock_registry();
    pp_panic("attempt to free an unknown or already-freed nums value");
}

void pp_push(pp_numbers *numbers, int64_t value) {
    if (numbers == NULL) pp_panic("null numbers list");
    size_t limit = SIZE_MAX / sizeof(*numbers->values);
    if ((uintmax_t)limit > (uintmax_t)INT64_MAX) limit = (size_t)INT64_MAX;
    if (numbers->length >= limit) pp_panic("numbers list is too large");
    if (numbers->length == numbers->capacity) {
        size_t capacity = numbers->capacity == 0 ? 8 : numbers->capacity;
        capacity = capacity > limit / 2 ? limit : capacity * 2;
        int64_t *values = realloc(numbers->values, capacity * sizeof(*values));
        if (values == NULL) pp_panic("out of memory");
        /* Only the list owns this reallocatable buffer, not the registry. */
        numbers->values = values;
        numbers->capacity = capacity;
    }
    numbers->values[numbers->length++] = value;
}

static size_t numbers_index(pp_numbers *numbers, int64_t index) {
    if (numbers == NULL) pp_panic("null numbers list");
    if (index < 0 || (uintmax_t)index >= (uintmax_t)numbers->length)
        pp_panic("numbers index out of bounds");
    return (size_t)index;
}

int64_t pp_at(pp_numbers *numbers, int64_t index) {
    const size_t slot = numbers_index(numbers, index);
    return numbers->values[slot];
}

void pp_put(pp_numbers *numbers, int64_t index, int64_t value) {
    const size_t slot = numbers_index(numbers, index);
    numbers->values[slot] = value;
}

int64_t pp_size(pp_numbers *numbers) {
    if (numbers == NULL) pp_panic("null numbers list");
    return (int64_t)numbers->length;
}

int64_t pp_pop(pp_numbers *numbers) {
    if (numbers == NULL) pp_panic("null numbers list");
    if (numbers->length == 0) pp_panic("pop from empty numbers list");
    return numbers->values[--numbers->length];
}

static int compare_numbers(const void *left, const void *right) {
    const int64_t a = *(const int64_t *)left;
    const int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

void pp_sort(pp_numbers *numbers) {
    if (numbers == NULL) pp_panic("null numbers list");
    if (numbers->length > 1)
        qsort(numbers->values, numbers->length, sizeof(*numbers->values), compare_numbers);
}

pp_i64_slice *pp_numbers_view(pp_numbers *numbers, int64_t start, int64_t end) {
    if (numbers == NULL) pp_panic("null numbers list");
    if (start < 0 || end < start || (uintmax_t)end > (uintmax_t)numbers->length)
        pp_panic("slice bounds out of range");
    pp_i64_slice *slice = malloc(sizeof(*slice));
    if (slice == NULL) pp_panic("out of memory");
    *slice = (pp_i64_slice){.owner = numbers, .start = (size_t)start, .length = (size_t)(end - start)};
    pp_lock_registry();
    slice->next = owned_slices;
    owned_slices = slice;
    pp_unlock_registry();
    return slice;
}

int64_t pp_slice_len_i64(pp_i64_slice *slice) {
    if (slice == NULL) pp_panic("null slice");
    return (int64_t)slice->length;
}

int64_t pp_slice_at_i64(pp_i64_slice *slice, int64_t index) {
    if (slice == NULL) pp_panic("null slice");
    if (index < 0 || (uintmax_t)index >= (uintmax_t)slice->length)
        pp_panic("slice index out of bounds");
    return pp_at(slice->owner, (int64_t)(slice->start + (size_t)index));
}

static char *own_text(char *value) {
    struct owned_text *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(value);
        pp_panic("out of memory");
    }
    pp_lock_registry();
    *entry = (struct owned_text){.value = value, .next = owned_strings};
    owned_strings = entry;
    pp_unlock_registry();
    return value;
}

const char *pp_adopt_text(char *value) {
    if (value == NULL) pp_panic("cannot adopt null text");
    return own_text(value);
}

static size_t text_size(size_t left, size_t right) {
    if (right > SIZE_MAX - left || left + right == SIZE_MAX ||
        (uintmax_t)(left + right) > (uintmax_t)INT64_MAX)
        pp_panic("string length exceeds runtime limits");
    return left + right + 1;
}

static char *new_text(size_t length) {
    char *value = malloc(text_size(length, 0));
    if (value == NULL) pp_panic("out of memory");
    return own_text(value);
}

const char *pp_concat(const char *left, const char *right) {
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    char *value = new_text(text_size(left_length, right_length) - 1);
    memcpy(value, left, left_length);
    memcpy(value + left_length, right, right_length + 1);
    return value;
}

const char *pp_slice(const char *text, int64_t start, int64_t end) {
    if (start < 0 || end < start || (uintmax_t)end > (uintmax_t)strlen(text))
        pp_panic("string slice out of bounds");
    const size_t length = (size_t)(end - start);
    char *value = new_text(length);
    memcpy(value, text + (size_t)start, length);
    value[length] = '\0';
    return value;
}

bool pp_contains(const char *text, const char *part) { return strstr(text, part) != NULL; }

const char *pp_read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) pp_panic("cannot open text file for reading");
    char *value = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const char *error = NULL;
    char chunk[4096];
    for (;;) {
        const size_t count = fread(chunk, 1, sizeof(chunk), file);
        if (ferror(file)) {
            error = "cannot read text file";
            break;
        }
        if (memchr(chunk, '\0', count) != NULL) {
            error = "text file contains embedded NUL";
            break;
        }
        const size_t needed = text_size(length, count);
        if (needed > capacity) {
            size_t grown = capacity == 0 ? sizeof(chunk) + 1 : capacity;
            while (grown < needed) {
                if (grown > SIZE_MAX / 2) {
                    grown = needed;
                    break;
                }
                grown *= 2;
            }
            char *buffer = realloc(value, grown);
            if (buffer == NULL) {
                error = "out of memory";
                break;
            }
            value = buffer;
            capacity = grown;
        }
        memcpy(value + length, chunk, count);
        length += count;
        if (feof(file)) break;
    }
    if (fclose(file) != 0) error = "cannot close text file after reading";
    if (error != NULL) {
        free(value);
        pp_panic(error);
    }
    value[length] = '\0';
    return own_text(value);
}

void pp_write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) pp_panic("cannot open text file for writing");
    const size_t length = strlen(text);
    bool failed = fwrite(text, 1, length, file) != length;
    if (ferror(file)) failed = true;
    if (fclose(file) != 0) failed = true;
    if (failed) pp_panic("cannot write text file");
}

bool pp_file_exists(const char *path) {
    if (path == NULL) pp_panic("null path");
    struct stat information;
    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

bool pp_make_dir(const char *path) {
    if (path == NULL || *path == '\0') return false;
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0777) == 0;
#endif
}

bool pp_remove_file(const char *path) {
    return path != NULL && *path != '\0' && remove(path) == 0;
}

bool pp_rename_file(const char *from, const char *to) {
    return from != NULL && to != NULL && *from != '\0' && *to != '\0' && rename(from, to) == 0;
}

const char *pp_path_join(const char *left, const char *right) {
    if (left == NULL || right == NULL) pp_panic("path_join received null text");
    const size_t a = strlen(left), b = strlen(right);
    const bool left_sep = a > 0 && (left[a - 1] == '/' || left[a - 1] == '\\');
    const bool right_sep = b > 0 && (right[0] == '/' || right[0] == '\\');
    const size_t middle = left_sep || right_sep || a == 0 || b == 0 ? 0 : 1;
    if (a > SIZE_MAX - b - middle - 1) pp_panic("joined path is too large");
    char *joined = malloc(a + b + middle + 1);
    if (joined == NULL) pp_panic("out of memory");
    memcpy(joined, left, a);
    size_t offset = a;
#ifdef _WIN32
    if (middle) joined[offset++] = '\\';
#else
    if (middle) joined[offset++] = '/';
#endif
    const size_t skip = left_sep && right_sep ? 1 : 0;
    memcpy(joined + offset, right + skip, b - skip);
    joined[offset + b - skip] = '\0';
    return own_text(joined);
}

static bool pp_utf8_scan(const char *text, int64_t *count) {
    if (text == NULL) return false;
    const unsigned char *cursor = (const unsigned char *)text;
    int64_t total = 0;
    while (*cursor) {
        uint32_t codepoint;
        size_t extra;
        if (cursor[0] < 0x80) { codepoint = cursor[0]; extra = 0; }
        else if ((cursor[0] & 0xe0) == 0xc0) { codepoint = cursor[0] & 0x1f; extra = 1; }
        else if ((cursor[0] & 0xf0) == 0xe0) { codepoint = cursor[0] & 0x0f; extra = 2; }
        else if ((cursor[0] & 0xf8) == 0xf0) { codepoint = cursor[0] & 0x07; extra = 3; }
        else return false;
        for (size_t i = 1; i <= extra; ++i) {
            if (cursor[i] == '\0' || (cursor[i] & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (cursor[i] & 0x3f);
        }
        if ((extra == 1 && codepoint < 0x80) || (extra == 2 && codepoint < 0x800) ||
            (extra == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        if (total == INT64_MAX) pp_panic("UTF-8 text is too long");
        ++total;
        cursor += extra + 1;
    }
    if (count) *count = total;
    return true;
}

bool pp_utf8_valid(const char *text) { return pp_utf8_scan(text, NULL); }

int64_t pp_utf8_len(const char *text) {
    int64_t count = 0;
    if (!pp_utf8_scan(text, &count)) pp_panic("invalid UTF-8 text");
    return count;
}

const char *pp_current_dir(void) {
#ifdef _WIN32
    DWORD needed = GetCurrentDirectoryA(0, NULL);
    if (needed == 0) pp_panic("cannot determine current directory");
    char *value = new_text((size_t)needed - 1);
    if (GetCurrentDirectoryA(needed, value) == 0) pp_panic("cannot determine current directory");
    return value;
#else
    size_t capacity = 256;
    for (;;) {
        char *buffer = malloc(capacity);
        if (buffer == NULL) pp_panic("out of memory");
        errno = 0;
        if (getcwd(buffer, capacity) != NULL) return own_text(buffer);
        const int error = errno;
        free(buffer);
        if (error != ERANGE) pp_panic("cannot determine current directory");
        if (capacity > SIZE_MAX / 2) pp_panic("current directory path is too long");
        capacity *= 2;
    }
#endif
}

bool pp_env_has(const char *name) {
    if (name == NULL || *name == '\0') pp_panic("environment variable name must not be empty");
    return getenv(name) != NULL;
}

const char *pp_env_or(const char *name, const char *fallback) {
    if (name == NULL || *name == '\0') pp_panic("environment variable name must not be empty");
    if (fallback == NULL) pp_panic("null environment fallback");
    const char *value = getenv(name);
    return value == NULL ? fallback : value;
}

const char *pp_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

const char *pp_read_line(void) {
    size_t length = 0;
    size_t capacity = 64;
    char *buffer = malloc(capacity);
    if (buffer == NULL) pp_panic("out of memory");
    for (;;) {
        const int ch = fgetc(stdin);
        if (ch == EOF || ch == '\n') break;
        if (ch == '\0') {
            free(buffer);
            pp_panic("stdin line contains embedded NUL");
        }
        if (length + 1 >= capacity) {
            if (capacity > SIZE_MAX / 2) {
                free(buffer);
                pp_panic("stdin line is too long");
            }
            capacity *= 2;
            char *grown = realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                pp_panic("out of memory");
            }
            buffer = grown;
        }
        buffer[length++] = (char)ch;
    }
    if (ferror(stdin)) {
        free(buffer);
        pp_panic("cannot read stdin");
    }
    if (length > 0 && buffer[length - 1] == '\r') --length;
    buffer[length] = '\0';
    return own_text(buffer);
}

void pp_sleep_ms(int64_t duration) {
    if (duration < 0) pp_panic("sleep duration must be nonnegative");
#ifdef _WIN32
    uint64_t remaining = (uint64_t)duration;
    while (remaining > 0) {
        const DWORD chunk = remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        Sleep(chunk);
        remaining -= chunk;
    }
#else
    struct timespec request = {duration / 1000, (duration % 1000) * 1000000};
    while (nanosleep(&request, &request) != 0) {
        if (errno != EINTR) pp_panic("sleep failed");
    }
#endif
}

pp_task *pp_task_spawn(pp_task_entry entry, const void *context, int64_t context_size) {
    if (entry == NULL) pp_panic("cannot spawn null async entry");
    if (context_size < 0 || (uintmax_t)context_size > (uintmax_t)SIZE_MAX)
        pp_panic("invalid async context size");
    pp_task *task = calloc(1, sizeof(*task));
    if (task == NULL) pp_panic("out of memory");
    task->entry = entry;
    atomic_init(&task->cancel_requested, false);
    atomic_init(&task->finished, false);
    if (context_size > 0) {
        if (context == NULL) { free(task); pp_panic("null async context"); }
        task->context = malloc((size_t)context_size);
        if (task->context == NULL) { free(task); pp_panic("out of memory"); }
        memcpy(task->context, context, (size_t)context_size);
    }
#ifdef _WIN32
    task->thread = CreateThread(NULL, 0, pp_task_worker, task, 0, NULL);
    if (task->thread == NULL) { free(task->context); free(task); pp_panic("cannot create async task"); }
#else
    if (pthread_create(&task->thread, NULL, pp_task_worker, task) != 0) {
        free(task->context);
        free(task);
        pp_panic("cannot create async task");
    }
#endif
    pp_lock_registry();
    task->next = owned_tasks;
    owned_tasks = task;
    pp_unlock_registry();
    return task;
}

void pp_task_cancel(pp_task *task) {
    if (task == NULL) pp_panic("cannot cancel null task");
    atomic_store_explicit(&task->cancel_requested, true, memory_order_release);
}

bool pp_task_is_done(pp_task *task) {
    if (task == NULL) pp_panic("cannot inspect null task");
    return atomic_load_explicit(&task->finished, memory_order_acquire);
}

bool pp_task_cancelled(void) {
    return current_task != NULL &&
           atomic_load_explicit(&current_task->cancel_requested, memory_order_acquire);
}

uintptr_t pp_task_await_bits(pp_task *task) {
    pp_task_join(task);
    return task->result;
}

int64_t pp_task_await_i64(pp_task *task) { return (int64_t)pp_task_await_bits(task); }

double pp_task_await_f64(pp_task *task) {
    const uint64_t bits = (uint64_t)pp_task_await_bits(task);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void *pp_task_await_ptr(pp_task *task) { return (void *)(uintptr_t)pp_task_await_bits(task); }

void pp_task_await_void(pp_task *task) { (void)pp_task_await_bits(task); }

const char *pp_text_int(int64_t value) {
    char buffer[32];
    const int length = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (length < 0 || (size_t)length >= sizeof(buffer)) pp_panic("cannot format integer");
    char *text = new_text((size_t)length);
    memcpy(text, buffer, (size_t)length + 1);
    return text;
}

int64_t pp_parse_int(const char *text) {
    const char *digits = text;
    if (*digits == '+' || *digits == '-') ++digits;
    if (*digits == '\0') pp_panic("invalid integer text");
    for (const char *p = digits; *p != '\0'; ++p)
        if (*p < '0' || *p > '9') pp_panic("invalid integer text");
    errno = 0;
    char *end;
    const intmax_t value = strtoimax(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || value < INT64_MIN || value > INT64_MAX)
        pp_panic("integer text out of range");
    return (int64_t)value;
}

double pp_decimal(int64_t value) { return (double)value; }

int64_t pp_whole(double value) {
    /* INT64_MAX rounds up to 2^63 as a double, so use an exclusive bound. */
    if (!isfinite(value) || value < -0x1p63 || value >= 0x1p63)
        pp_panic("decimal out of integer range");
    return (int64_t)value;
}

void pp_assert(bool condition, const char *message) {
    if (!condition) pp_panic(message);
}

void pp_print_int(int64_t value) { printf("%" PRId64, value); }
void pp_print_float(double value) { printf("%.15g", value); }
void pp_print_bool(bool value) { fputs(value ? "yes" : "no", stdout); }
void pp_print_str(const char *value) { fputs(value, stdout); }
void pp_println_int(int64_t value) { printf("%" PRId64 "\n", value); }
void pp_println_float(double value) { printf("%.15g\n", value); }
void pp_println_bool(bool value) { puts(value ? "yes" : "no"); }
void pp_println_str(const char *value) { puts(value); }

int64_t pp_len(const char *value) {
    const size_t length = strlen(value);
    if (length > INT64_MAX) pp_panic("string length exceeds int range");
    return (int64_t)length;
}

bool pp_str_eq(const char *left, const char *right) { return strcmp(left, right) == 0; }

void pp_panic(const char *message) {
    fprintf(stderr, "punpun panic: %s\n", message);
    abort();
}

int64_t pp_clock_ms(void) {
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) pp_panic("clock_gettime failed");
    return now.tv_sec * INT64_C(1000) + now.tv_nsec / INT64_C(1000000);
#endif
}

int64_t pp_abs_i64(int64_t value) {
    if (value == INT64_MIN) pp_panic("integer overflow in abs");
    return value < 0 ? -value : value;
}

static int64_t integer_error(const char *operation) {
    char message[96];
    snprintf(message, sizeof(message), "integer error in %s", operation);
    pp_panic(message);
    return 0; /* pp_panic aborts; keeps conservative C compilers satisfied. */
}

int64_t pp_add_i64(int64_t left, int64_t right) {
    int64_t result;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(left, right, &result)) return integer_error("addition");
#else
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
        return integer_error("addition");
    result = left + right;
#endif
    return result;
}

int64_t pp_sub_i64(int64_t left, int64_t right) {
    int64_t result;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_sub_overflow(left, right, &result)) return integer_error("subtraction");
#else
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right))
        return integer_error("subtraction");
    result = left - right;
#endif
    return result;
}

int64_t pp_mul_i64(int64_t left, int64_t right) {
    int64_t result;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_mul_overflow(left, right, &result)) return integer_error("multiplication");
#else
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left))
            return integer_error("multiplication");
    } else if (left < 0) {
        if ((right > 0 && left < INT64_MIN / right) ||
            (right < 0 && left != 0 && right < INT64_MAX / left))
            return integer_error("multiplication");
    }
    result = left * right;
#endif
    return result;
}

int64_t pp_div_i64(int64_t left, int64_t right) {
    if (right == 0 || (left == INT64_MIN && right == -1)) return integer_error("division");
    return left / right;
}

int64_t pp_mod_i64(int64_t left, int64_t right) {
    if (right == 0 || (left == INT64_MIN && right == -1)) return integer_error("remainder");
    return left % right;
}

int64_t pp_neg_i64(int64_t value) {
    if (value == INT64_MIN) return integer_error("negation");
    return -value;
}
