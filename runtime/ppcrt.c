/* ppc runtime library implementation. C11, POSIX, no external dependencies. */
#define _POSIX_C_SOURCE 200809L

#include "ppcrt.h"

#include "ppc_platform.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Ownership bookkeeping                                              */
/*                                                                    */
/* Every allocation the runtime hands to PunPun code is recorded here */
/* and released in one sweep at exit. PunPun has no incremental GC, so */
/* this is what makes "callers never free" a safe rule.               */
/* ------------------------------------------------------------------ */

typedef struct pp_owned {
    void *pointer;
    void (*release)(void *);
    struct pp_owned *next;
} pp_owned;

static pp_owned *g_owned = NULL;
static ppc_plat_mutex *g_owned_lock = NULL;
/* Defined further down with the task and task-group code, but created here in
 * pp_runtime_init because every one of them must exist before any task can be
 * spawned, and init is the single point that is guaranteed to run first. */
static ppc_plat_mutex *g_group_table_lock;
static ppc_plat_tls *g_current_task_key;
static int g_initialized = 0;
static int g_cleaned = 0;
static int g_argc = 0;
static char **g_argv = NULL;

static void pp_track(void *pointer, void (*release)(void *)) {
    if (!pointer) return;
    pp_owned *node = (pp_owned *)malloc(sizeof(pp_owned));
    if (!node) {
        fputs("punpun: out of memory\n", stderr);
        abort();
    }
    node->pointer = pointer;
    node->release = release;
    ppc_plat_mutex_lock(g_owned_lock);
    node->next = g_owned;
    g_owned = node;
    ppc_plat_mutex_unlock(g_owned_lock);
}

static void *pp_alloc(size_t size) {
    void *memory = malloc(size ? size : 1);
    if (!memory) {
        fputs("punpun: out of memory\n", stderr);
        abort();
    }
    return memory;
}

void pp_panic(const char *message) {
    fflush(stdout);
    fprintf(stderr, "punpun: panic: %s\n", message ? message : "(no message)");
    fflush(stderr);
    /* Panics abort rather than unwind, which is why 0.6 promises lexical
     * destruction only on normal control flow. */
    abort();
}

void pp_assert(bool condition, const char *message) {
    if (!condition) pp_panic(message ? message : "assertion failed");
}

int32_t pp_runtime_abi_version(void) { return PUNPUN_RUNTIME_ABI_VERSION; }

void pp_runtime_cleanup(void) {
    if (g_cleaned) return;
    g_cleaned = 1;
    ppc_plat_mutex_lock(g_owned_lock);
    pp_owned *node = g_owned;
    g_owned = NULL;
    ppc_plat_mutex_unlock(g_owned_lock);
    while (node) {
        pp_owned *next = node->next;
        if (node->release) node->release(node->pointer);
        else free(node->pointer);
        free(node);
        node = next;
    }
}

void pp_runtime_init(int argc, char **argv) {
    if (g_initialized) return;
    g_initialized = 1;
    ppc_plat_init();
    pp_https_runtime_init();
    pp_gui_runtime_init();
    /* The ownership lock must exist before anything can allocate through
     * pp_track, so it is created first and never destroyed: cleanup runs while
     * other threads may still be finishing. */
    if (!g_owned_lock) g_owned_lock = ppc_plat_mutex_new();
    if (!g_group_table_lock) g_group_table_lock = ppc_plat_mutex_new();
    if (!g_current_task_key) g_current_task_key = ppc_plat_tls_new();
    g_argc = argc;
    g_argv = argv; /* borrowed until cleanup */
    atexit(pp_runtime_cleanup);
}

void *pp_object_alloc(int64_t size) {
    if (size < 0) pp_panic("negative allocation size");
    void *memory = pp_alloc((size_t)size);
    memset(memory, 0, (size_t)size);
    pp_track(memory, NULL);
    return memory;
}

void pp_object_free(void *pointer) {
    /* Objects are released in bulk at cleanup. An explicit `drop` marks the
     * binding dead in the compiler but does not return memory early, which
     * keeps every runtime pointer valid for the whole program. */
    (void)pointer;
}

/* ------------------------------------------------------------------ */
/* Program arguments                                                  */
/* ------------------------------------------------------------------ */

int64_t pp_arg_count(void) {
    /* argv[0] is the program name and is not counted as an argument. */
    return g_argc > 0 ? (int64_t)(g_argc - 1) : 0;
}

const char *pp_arg(int64_t index) {
    if (index < 0 || index >= pp_arg_count()) {
        char message[96];
        snprintf(message, sizeof(message),
                 "argument index %" PRId64 " is out of range (0..%" PRId64 ")", index,
                 pp_arg_count() - 1);
        pp_panic(message);
    }
    return g_argv[index + 1];
}

/* ------------------------------------------------------------------ */
/* nums                                                               */
/* ------------------------------------------------------------------ */

struct pp_numbers {
    int64_t *data;
    int64_t length;
    int64_t capacity;
};

static void pp_numbers_release(void *pointer) {
    pp_numbers *numbers = (pp_numbers *)pointer;
    free(numbers->data);
    free(numbers);
}

pp_numbers *pp_numbers_new(void) {
    pp_numbers *numbers = (pp_numbers *)pp_alloc(sizeof(pp_numbers));
    numbers->data = NULL;
    numbers->length = 0;
    numbers->capacity = 0;
    pp_track(numbers, pp_numbers_release);
    return numbers;
}

void pp_numbers_free(pp_numbers *numbers) { (void)numbers; }

void pp_push(pp_numbers *numbers, int64_t value) {
    if (!numbers) pp_panic("push on a null list");
    if (numbers->length == numbers->capacity) {
        /* Geometric growth keeps repeated push amortized constant, which the
         * sieve benchmark leans on heavily. */
        int64_t capacity = numbers->capacity ? numbers->capacity * 2 : 8;
        int64_t *data = (int64_t *)realloc(numbers->data, (size_t)capacity * sizeof(int64_t));
        if (!data) pp_panic("out of memory growing a list");
        numbers->data = data;
        numbers->capacity = capacity;
    }
    numbers->data[numbers->length++] = value;
}

int64_t pp_at(pp_numbers *numbers, int64_t index) {
    if (!numbers) pp_panic("index on a null list");
    if (index < 0 || index >= numbers->length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "list index %" PRId64 " is out of range (length %" PRId64 ")", index,
                 numbers->length);
        pp_panic(message);
    }
    return numbers->data[index];
}

void pp_put(pp_numbers *numbers, int64_t index, int64_t value) {
    if (!numbers) pp_panic("assignment into a null list");
    if (index < 0 || index >= numbers->length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "list index %" PRId64 " is out of range (length %" PRId64 ")", index,
                 numbers->length);
        pp_panic(message);
    }
    numbers->data[index] = value;
}

int64_t pp_size(pp_numbers *numbers) { return numbers ? numbers->length : 0; }

int64_t pp_pop(pp_numbers *numbers) {
    if (!numbers || numbers->length == 0) pp_panic("pop from an empty list");
    return numbers->data[--numbers->length];
}

static int pp_compare_i64(const void *left, const void *right) {
    const int64_t a = *(const int64_t *)left;
    const int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

void pp_sort(pp_numbers *numbers) {
    if (!numbers || numbers->length < 2) return;
    qsort(numbers->data, (size_t)numbers->length, sizeof(int64_t), pp_compare_i64);
}

/* ------------------------------------------------------------------ */
/* List<T>                                                            */
/*                                                                    */
/* A growable array of 8-byte slots. The compiler has already checked  */
/* that every element has the declared type, so no tag is stored and   */
/* no check is repeated here. Bounds ARE checked, because an index is  */
/* a runtime value the compiler cannot constrain.                      */
/* ------------------------------------------------------------------ */

struct pp_list {
    int64_t *slots;
    int64_t length;
    int64_t capacity;
};

static void pp_list_release(void *pointer) {
    pp_list *list = (pp_list *)pointer;
    free(list->slots);
    free(list);
}

int64_t pp_bits_from_f64(double value) {
    int64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double pp_f64_from_bits(int64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

pp_list *pp_list_new(void) {
    pp_list *list = (pp_list *)pp_alloc(sizeof(pp_list));
    list->slots = NULL;
    list->length = 0;
    list->capacity = 0;
    pp_track(list, pp_list_release);
    return list;
}

void pp_list_push(pp_list *list, int64_t slot) {
    if (!list) pp_panic("push on a null list");
    if (list->length == list->capacity) {
        /* Geometric growth keeps repeated push amortized constant. */
        const int64_t capacity = list->capacity ? list->capacity * 2 : 8;
        int64_t *slots = (int64_t *)realloc(list->slots, (size_t)capacity * sizeof(int64_t));
        if (!slots) pp_panic("out of memory growing a list");
        list->slots = slots;
        list->capacity = capacity;
    }
    list->slots[list->length++] = slot;
}

static void pp_list_check(pp_list *list, int64_t index) {
    if (!list) pp_panic("index on a null list");
    if (index < 0 || index >= list->length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "list index %" PRId64 " is out of range (length %" PRId64 ")", index,
                 list->length);
        pp_panic(message);
    }
}

int64_t pp_list_at(pp_list *list, int64_t index) {
    pp_list_check(list, index);
    return list->slots[index];
}

void pp_list_put(pp_list *list, int64_t index, int64_t slot) {
    pp_list_check(list, index);
    list->slots[index] = slot;
}

int64_t pp_list_size(pp_list *list) { return list ? list->length : 0; }

int64_t pp_list_pop(pp_list *list) {
    if (!list || list->length == 0) pp_panic("pop from an empty list");
    return list->slots[--list->length];
}

void pp_list_clear(pp_list *list) {
    if (list) list->length = 0;
}


/* ------------------------------------------------------------------ */
/* Map<V>: string-keyed hash map                                      */
/*                                                                    */
/* Open addressing with linear probing and tombstones. Chosen over    */
/* chaining because it needs one allocation for the table rather than */
/* one per entry, and the load factor is kept at 0.7 so probe chains  */
/* stay short.                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char *key;          /* owned copy; NULL means empty */
    int64_t value;
    uint64_t hash;
    int tombstone;      /* removed, but still terminates no probe chain */
} pp_map_entry;

struct pp_map {
    pp_map_entry *entries;
    int64_t count;      /* live entries */
    int64_t used;       /* live + tombstones, for load factor */
    int64_t capacity;   /* always a power of two */
};

/* FNV-1a. Fast, adequate spread for identifier-shaped keys, and it has no
 * dependencies. Not collision-resistant, which does not matter here: a
 * collision costs one extra probe, never a wrong answer, because the full key
 * is compared before a match is accepted. */
static uint64_t pp_hash_key(const char *key) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *c = (const unsigned char *)key; *c; ++c) {
        hash ^= *c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static void pp_map_release(void *pointer) {
    pp_map *map = (pp_map *)pointer;
    for (int64_t i = 0; i < map->capacity; ++i) free(map->entries[i].key);
    free(map->entries);
    free(map);
}

pp_map *pp_map_new(void) {
    pp_map *map = (pp_map *)pp_alloc(sizeof(pp_map));
    map->entries = NULL;
    map->count = 0;
    map->used = 0;
    map->capacity = 0;
    pp_track(map, pp_map_release);
    return map;
}

/* Returns the slot a key belongs in: its entry if present, otherwise the first
 * free slot on its probe chain. Tombstones are reusable for insertion but must
 * not stop the search, or a key placed after one would become unreachable. */
static pp_map_entry *pp_map_find(pp_map_entry *entries, int64_t capacity,
                                 const char *key, uint64_t hash) {
    int64_t index = (int64_t)(hash & (uint64_t)(capacity - 1));
    pp_map_entry *first_tombstone = NULL;
    for (;;) {
        pp_map_entry *entry = &entries[index];
        if (!entry->key) {
            if (entry->tombstone) {
                if (!first_tombstone) first_tombstone = entry;
            } else {
                return first_tombstone ? first_tombstone : entry;
            }
        } else if (entry->hash == hash && strcmp(entry->key, key) == 0) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void pp_map_grow(pp_map *map) {
    const int64_t capacity = map->capacity ? map->capacity * 2 : 16;
    pp_map_entry *entries = (pp_map_entry *)calloc((size_t)capacity, sizeof(pp_map_entry));
    if (!entries) pp_panic("out of memory growing a map");

    /* Rehashing drops tombstones, which is the only thing that reclaims them. */
    for (int64_t i = 0; i < map->capacity; ++i) {
        pp_map_entry *old = &map->entries[i];
        if (!old->key) continue;
        pp_map_entry *slot = pp_map_find(entries, capacity, old->key, old->hash);
        *slot = *old;
        slot->tombstone = 0;
    }
    free(map->entries);
    map->entries = entries;
    map->capacity = capacity;
    map->used = map->count;
}

void pp_map_put(pp_map *map, const char *key, int64_t slot) {
    if (!map) pp_panic("put on a null map");
    if (!key) pp_panic("a map key cannot be null");
    /* Grow at 70% of capacity: past that, linear probing degrades sharply. */
    if (map->capacity == 0 || (map->used + 1) * 10 >= map->capacity * 7) pp_map_grow(map);

    const uint64_t hash = pp_hash_key(key);
    pp_map_entry *entry = pp_map_find(map->entries, map->capacity, key, hash);
    if (!entry->key) {
        /* The map owns its keys, so the caller may reuse the string it passed. */
        const size_t length = strlen(key);
        entry->key = (char *)pp_alloc(length + 1);
        memcpy(entry->key, key, length + 1);
        entry->hash = hash;
        ++map->count;
        if (!entry->tombstone) ++map->used;
        entry->tombstone = 0;
    }
    entry->value = slot;
}

int64_t pp_map_get(pp_map *map, const char *key) {
    if (!map || map->capacity == 0) {
        char message[128];
        snprintf(message, sizeof(message), "map has no key '%s'", key ? key : "");
        pp_panic(message);
    }
    pp_map_entry *entry = pp_map_find(map->entries, map->capacity, key, pp_hash_key(key));
    if (!entry->key) {
        char message[128];
        snprintf(message, sizeof(message), "map has no key '%s'", key ? key : "");
        pp_panic(message);
    }
    return entry->value;
}

int64_t pp_map_get_or(pp_map *map, const char *key, int64_t fallback) {
    if (!map || map->capacity == 0) return fallback;
    pp_map_entry *entry = pp_map_find(map->entries, map->capacity, key, pp_hash_key(key));
    return entry->key ? entry->value : fallback;
}

bool pp_map_has(pp_map *map, const char *key) {
    if (!map || map->capacity == 0) return false;
    return pp_map_find(map->entries, map->capacity, key, pp_hash_key(key))->key != NULL;
}

bool pp_map_remove(pp_map *map, const char *key) {
    if (!map || map->capacity == 0) return false;
    pp_map_entry *entry = pp_map_find(map->entries, map->capacity, key, pp_hash_key(key));
    if (!entry->key) return false;
    free(entry->key);
    entry->key = NULL;
    entry->value = 0;
    /* A tombstone rather than an empty slot: clearing it outright would cut
     * every probe chain that passes through this position. */
    entry->tombstone = 1;
    --map->count;
    return true;
}

int64_t pp_map_size(pp_map *map) { return map ? map->count : 0; }

pp_list *pp_map_keys(pp_map *map) {
    pp_list *keys = pp_list_new();
    if (!map) return keys;
    for (int64_t i = 0; i < map->capacity; ++i) {
        if (map->entries[i].key) pp_list_push(keys, (int64_t)(intptr_t)map->entries[i].key);
    }
    return keys;
}

void pp_map_clear(pp_map *map) {
    if (!map) return;
    for (int64_t i = 0; i < map->capacity; ++i) {
        free(map->entries[i].key);
        map->entries[i].key = NULL;
        map->entries[i].tombstone = 0;
    }
    map->count = 0;
    map->used = 0;
}

/* ------------------------------------------------------------------ */
/* bytes: a mutable byte buffer                                       */
/* ------------------------------------------------------------------ */

struct pp_bytes {
    unsigned char *data;
    int64_t length;
    int64_t capacity;
};

static void pp_bytes_release(void *pointer) {
    pp_bytes *bytes = (pp_bytes *)pointer;
    free(bytes->data);
    free(bytes);
}

pp_bytes *pp_bytes_new(void) {
    pp_bytes *bytes = (pp_bytes *)pp_alloc(sizeof(pp_bytes));
    bytes->data = NULL;
    bytes->length = 0;
    bytes->capacity = 0;
    pp_track(bytes, pp_bytes_release);
    return bytes;
}

void pp_bytes_push(pp_bytes *bytes, int64_t value) {
    if (!bytes) pp_panic("push on a null byte buffer");
    if (value < 0 || value > 255) pp_panic("a byte value must be in 0..255");
    if (bytes->length == bytes->capacity) {
        const int64_t capacity = bytes->capacity ? bytes->capacity * 2 : 32;
        unsigned char *data = (unsigned char *)realloc(bytes->data, (size_t)capacity);
        if (!data) pp_panic("out of memory growing a byte buffer");
        bytes->data = data;
        bytes->capacity = capacity;
    }
    bytes->data[bytes->length++] = (unsigned char)value;
}

pp_bytes *pp_bytes_from_text(const char *text) {
    pp_bytes *bytes = pp_bytes_new();
    if (!text) return bytes;
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c) {
        pp_bytes_push(bytes, *c);
    }
    return bytes;
}

const char *pp_bytes_to_text(pp_bytes *bytes) {
    if (!bytes) return "";
    /* A str carries no NUL, so a buffer containing one cannot be converted:
     * the result would silently truncate. */
    for (int64_t i = 0; i < bytes->length; ++i) {
        if (bytes->data[i] == 0) pp_panic("byte buffer contains a NUL and is not text");
    }
    char *text = (char *)pp_alloc((size_t)bytes->length + 1);
    memcpy(text, bytes->data, (size_t)bytes->length);
    text[bytes->length] = '\0';
    pp_track(text, NULL);
    return text;
}

static void pp_bytes_check(pp_bytes *bytes, int64_t index) {
    if (!bytes) pp_panic("index on a null byte buffer");
    if (index < 0 || index >= bytes->length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "byte index %" PRId64 " is out of range (length %" PRId64 ")", index,
                 bytes->length);
        pp_panic(message);
    }
}

int64_t pp_bytes_at(pp_bytes *bytes, int64_t index) {
    pp_bytes_check(bytes, index);
    return bytes->data[index];
}

void pp_bytes_put(pp_bytes *bytes, int64_t index, int64_t value) {
    pp_bytes_check(bytes, index);
    if (value < 0 || value > 255) pp_panic("a byte value must be in 0..255");
    bytes->data[index] = (unsigned char)value;
}

int64_t pp_bytes_len(pp_bytes *bytes) { return bytes ? bytes->length : 0; }

pp_bytes *pp_bytes_slice(pp_bytes *bytes, int64_t start, int64_t end) {
    if (!bytes) pp_panic("slice of a null byte buffer");
    if (start < 0 || end < start || end > bytes->length) {
        char message[128];
        snprintf(message, sizeof(message),
                 "byte slice [%" PRId64 ", %" PRId64 ") is out of range (length %" PRId64 ")",
                 start, end, bytes->length);
        pp_panic(message);
    }
    pp_bytes *out = pp_bytes_new();
    for (int64_t i = start; i < end; ++i) pp_bytes_push(out, bytes->data[i]);
    return out;
}

pp_bytes *pp_bytes_concat(pp_bytes *left, pp_bytes *right) {
    pp_bytes *out = pp_bytes_new();
    if (left) for (int64_t i = 0; i < left->length; ++i) pp_bytes_push(out, left->data[i]);
    if (right) for (int64_t i = 0; i < right->length; ++i) pp_bytes_push(out, right->data[i]);
    return out;
}

/* ------------------------------------------------------------------ */
/* Slices                                                             */
/* ------------------------------------------------------------------ */

struct pp_i64_slice {
    pp_numbers *source;
    int64_t start;
    int64_t length;
};

pp_i64_slice *pp_numbers_view(pp_numbers *numbers, int64_t start, int64_t end) {
    if (!numbers) pp_panic("view of a null list");
    if (start < 0 || end < start || end > numbers->length) {
        char message[128];
        snprintf(message, sizeof(message),
                 "view range [%" PRId64 ", %" PRId64 ") is out of range (length %" PRId64 ")",
                 start, end, numbers->length);
        pp_panic(message);
    }
    pp_i64_slice *slice = (pp_i64_slice *)pp_alloc(sizeof(pp_i64_slice));
    slice->source = numbers;
    slice->start = start;
    slice->length = end - start;
    pp_track(slice, NULL);
    return slice;
}

int64_t pp_slice_len_i64(pp_i64_slice *slice) { return slice ? slice->length : 0; }

int64_t pp_slice_at_i64(pp_i64_slice *slice, int64_t index) {
    if (!slice) pp_panic("index on a null slice");
    if (index < 0 || index >= slice->length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "slice index %" PRId64 " is out of range (length %" PRId64 ")", index,
                 slice->length);
        pp_panic(message);
    }
    /* The compiler forbids mutating the source while a named view is live, so
     * re-reading through the handle here is safe. */
    return slice->source->data[slice->start + index];
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

const char *pp_adopt_text(char *owned_text) {
    if (!owned_text) return "";
    pp_track(owned_text, NULL);
    return owned_text;
}

static char *pp_new_text(size_t length) {
    char *text = (char *)pp_alloc(length + 1);
    text[length] = '\0';
    pp_track(text, NULL);
    return text;
}

const char *pp_concat(const char *left, const char *right) {
    if (!left) left = "";
    if (!right) right = "";
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    char *text = pp_new_text(left_length + right_length);
    memcpy(text, left, left_length);
    memcpy(text + left_length, right, right_length);
    return text;
}

const char *pp_slice(const char *text, int64_t start, int64_t end) {
    if (!text) text = "";
    const int64_t length = (int64_t)strlen(text);
    if (start < 0 || end < start || end > length) {
        char message[128];
        snprintf(message, sizeof(message),
                 "text slice [%" PRId64 ", %" PRId64 ") is out of range (length %" PRId64 ")",
                 start, end, length);
        pp_panic(message);
    }
    char *result = pp_new_text((size_t)(end - start));
    memcpy(result, text + start, (size_t)(end - start));
    return result;
}

bool pp_contains(const char *text, const char *part) {
    if (!text || !part) return false;
    return strstr(text, part) != NULL;
}

int64_t pp_len(const char *value) { return value ? (int64_t)strlen(value) : 0; }

bool pp_str_eq(const char *left, const char *right) {
    if (left == right) return true;
    if (!left || !right) return false;
    return strcmp(left, right) == 0;
}

int64_t pp_str_cmp(const char *left, const char *right) {
    if (!left) left = "";
    if (!right) right = "";
    const int result = strcmp(left, right);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

const char *pp_text_int(int64_t value) {
    char buffer[32];
    const int written = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    char *text = pp_new_text((size_t)written);
    memcpy(text, buffer, (size_t)written);
    return text;
}

int64_t pp_parse_int(const char *text) {
    if (!text || !*text) pp_panic("cannot parse an empty string as an int");
    const char *cursor = text;
    int negative = 0;
    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        ++cursor;
    }
    if (!*cursor) pp_panic("cannot parse this string as an int");

    /* Accumulate negatively so INT64_MIN is representable without overflow. */
    int64_t accumulator = 0;
    for (; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            char message[96];
            snprintf(message, sizeof(message), "cannot parse '%s' as an int", text);
            pp_panic(message);
        }
        const int digit = *cursor - '0';
        if (accumulator < (INT64_MIN + digit) / 10) pp_panic("integer literal is out of range");
        accumulator = accumulator * 10 - digit;
    }
    if (!negative) {
        if (accumulator == INT64_MIN) pp_panic("integer literal is out of range");
        accumulator = -accumulator;
    }
    return accumulator;
}

double pp_decimal(int64_t value) { return (double)value; }

int64_t pp_whole(double value) {
    if (value != value) pp_panic("cannot convert NaN to an int");
    if (value >= 9223372036854775808.0 || value < -9223372036854775808.0) {
        pp_panic("float value is out of int range");
    }
    return (int64_t)value; /* truncates toward zero */
}

bool pp_utf8_valid(const char *text) {
    if (!text) return true;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        unsigned char lead = *cursor++;
        int trailing;
        unsigned int code;
        if (lead < 0x80) continue;
        else if ((lead & 0xE0) == 0xC0) { trailing = 1; code = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { trailing = 2; code = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { trailing = 3; code = lead & 0x07; }
        else return false;

        for (int i = 0; i < trailing; ++i) {
            if ((*cursor & 0xC0) != 0x80) return false;
            code = (code << 6) | (*cursor++ & 0x3F);
        }
        /* Reject overlong forms, surrogates, and out-of-range scalars. */
        if (trailing == 1 && code < 0x80) return false;
        if (trailing == 2 && code < 0x800) return false;
        if (trailing == 3 && code < 0x10000) return false;
        if (code > 0x10FFFF) return false;
        if (code >= 0xD800 && code <= 0xDFFF) return false;
    }
    return true;
}

int64_t pp_utf8_len(const char *text) {
    if (!pp_utf8_valid(text)) pp_panic("text is not valid UTF-8");
    int64_t count = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if ((*cursor & 0xC0) != 0x80) ++count;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Filesystem                                                         */
/* ------------------------------------------------------------------ */

const char *pp_read_text(const char *path) {
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        char message[256];
        snprintf(message, sizeof(message), "cannot read '%s': %s", path, strerror(errno));
        pp_panic(message);
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        pp_panic("cannot determine file size");
    }
    const long size = ftell(stream);
    if (size < 0) {
        fclose(stream);
        pp_panic("cannot determine file size");
    }
    rewind(stream);

    char *text = pp_new_text((size_t)size);
    const size_t read = fread(text, 1, (size_t)size, stream);
    fclose(stream);
    text[read] = '\0';

    /* An embedded NUL would silently truncate the value, so it is rejected
     * rather than producing a string shorter than the file. */
    if (memchr(text, '\0', read) != NULL) {
        char message[256];
        snprintf(message, sizeof(message), "file '%s' contains a NUL byte", path);
        pp_panic(message);
    }
    return text;
}

void pp_write_text(const char *path, const char *text) {
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        char message[256];
        snprintf(message, sizeof(message), "cannot write '%s': %s", path, strerror(errno));
        pp_panic(message);
    }
    const size_t length = text ? strlen(text) : 0;
    const size_t written = length ? fwrite(text, 1, length, stream) : 0;
    const int closed = fclose(stream);
    if (written != length || closed != 0) pp_panic("write failed");
}

bool pp_file_exists(const char *path) { return ppc_plat_file_exists(path); }
bool pp_make_dir(const char *path) { return ppc_plat_make_dir(path); }
bool pp_remove_file(const char *path) { return ppc_plat_remove_file(path); }
bool pp_rename_file(const char *from, const char *to) {
    return ppc_plat_rename(from, to);
}

const char *pp_path_join(const char *left, const char *right) {
    if (!left || !*left) return right ? right : "";
    if (!right || !*right) return left;
    if (PPC_IS_SEPARATOR(right[0])) return right;
    const size_t left_length = strlen(left);
    const int needs_separator = !PPC_IS_SEPARATOR(left[left_length - 1]);
    const size_t right_length = strlen(right);
    char *text = pp_new_text(left_length + (size_t)needs_separator + right_length);
    memcpy(text, left, left_length);
    if (needs_separator) text[left_length] = PPC_PATH_SEPARATOR;
    memcpy(text + left_length + (size_t)needs_separator, right, right_length);
    return text;
}

const char *pp_current_dir(void) {
    char buffer[4096];
    if (!ppc_plat_current_dir(buffer, sizeof(buffer))) {
        pp_panic("cannot read the current directory");
    }
    const size_t length = strlen(buffer);
    char *text = pp_new_text(length);
    memcpy(text, buffer, length);
    return text;
}

/* ------------------------------------------------------------------ */
/* Environment and process                                            */
/* ------------------------------------------------------------------ */

bool pp_env_has(const char *name) { return ppc_plat_get_env(name) != NULL; }

const char *pp_env_or(const char *name, const char *fallback) {
    const char *value = ppc_plat_get_env(name);
    return value ? value : (fallback ? fallback : "");
}

const char *pp_platform(void) { return ppc_plat_name(); }

const char *pp_read_line(void) {
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = (char *)pp_alloc(capacity);

    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *grown = (char *)realloc(buffer, capacity);
            if (!grown) {
                free(buffer);
                pp_panic("out of memory reading a line");
            }
            buffer = grown;
        }
        buffer[length++] = (char)c;
    }
    /* Strip a trailing CR so CRLF input behaves the same as LF input. */
    if (length > 0 && buffer[length - 1] == '\r') --length;
    buffer[length] = '\0';
    pp_track(buffer, NULL);
    return buffer;
}

void pp_sleep_ms(int64_t duration) {
    if (duration < 0) pp_panic("sleep duration must be nonnegative");
    /* Sleeping is a cooperative cancellation safe point. The wait is broken
     * into small chunks so a cancelled task stops promptly, which is what lets
     * a group be cancelled without an event loop or forcibly killing a thread. */
    int64_t remaining = duration;
    while (remaining > 0) {
        if (pp_task_cancelled()) return;
        const int64_t chunk = remaining > 10 ? 10 : remaining;
        ppc_plat_sleep_ms(chunk);
        remaining -= chunk;
    }
}

int64_t pp_clock_ms(void) { return ppc_plat_monotonic_ms(); }

/* ------------------------------------------------------------------ */
/* Console                                                            */
/* ------------------------------------------------------------------ */

void pp_print_int(int64_t value) { printf("%" PRId64, value); }
void pp_print_float(double value) { printf("%g", value); }
void pp_print_bool(bool value) { fputs(value ? "yes" : "no", stdout); }
void pp_print_str(const char *value) { fputs(value ? value : "", stdout); }

void pp_println_int(int64_t value) { printf("%" PRId64 "\n", value); }
void pp_println_float(double value) { printf("%g\n", value); }
void pp_println_bool(bool value) { fputs(value ? "yes\n" : "no\n", stdout); }
void pp_println_str(const char *value) {
    fputs(value ? value : "", stdout);
    fputc('\n', stdout);
}

/* ------------------------------------------------------------------ */
/* Checked arithmetic                                                 */
/*                                                                    */
/* PunPun traps on overflow instead of wrapping, so every operation    */
/* that can overflow goes through one of these.                       */
/* ------------------------------------------------------------------ */

int64_t pp_add_i64(int64_t left, int64_t right) {
    int64_t result;
    if (__builtin_add_overflow(left, right, &result)) pp_panic("integer overflow in addition");
    return result;
}

int64_t pp_sub_i64(int64_t left, int64_t right) {
    int64_t result;
    if (__builtin_sub_overflow(left, right, &result)) pp_panic("integer overflow in subtraction");
    return result;
}

int64_t pp_mul_i64(int64_t left, int64_t right) {
    int64_t result;
    if (__builtin_mul_overflow(left, right, &result)) pp_panic("integer overflow in multiplication");
    return result;
}

int64_t pp_div_i64(int64_t left, int64_t right) {
    if (right == 0) pp_panic("division by zero");
    if (left == INT64_MIN && right == -1) pp_panic("integer overflow in division");
    return left / right;
}

int64_t pp_mod_i64(int64_t left, int64_t right) {
    if (right == 0) pp_panic("remainder by zero");
    if (left == INT64_MIN && right == -1) pp_panic("integer overflow in remainder");
    return left % right;
}

int64_t pp_neg_i64(int64_t value) {
    if (value == INT64_MIN) pp_panic("integer overflow in negation");
    return -value;
}

int64_t pp_abs_i64(int64_t value) {
    if (value == INT64_MIN) pp_panic("integer overflow in absolute value");
    return value < 0 ? -value : value;
}

int64_t pp_shl_i64(int64_t value, int64_t amount) {
    if (amount < 0 || amount >= 64) pp_panic("shift amount is out of range");
    return (int64_t)((uint64_t)value << amount);
}

int64_t pp_shr_i64(int64_t value, int64_t amount) {
    if (amount < 0 || amount >= 64) pp_panic("shift amount is out of range");
    return value >> amount;
}

/* ------------------------------------------------------------------ */
/* Async tasks                                                        */
/*                                                                    */
/* A task is one detached-joinable pthread. The runtime copies the     */
/* context block before starting the worker so generated code may pass */
/* a stack-backed call frame. Programs that never spawn create no      */
/* threads at all.                                                     */
/* ------------------------------------------------------------------ */

struct pp_task {
    ppc_plat_thread *thread;
    pp_task_entry entry;
    void *context;
    uintptr_t result;
    int done;
    int joined;
    int cancel_requested;
    int started;
    ppc_plat_mutex *lock;
};

/* Identifies the task running on the current thread, so cancelled() can find
 * it without being passed a handle. Created once in pp_runtime_init, which
 * every program calls before any task can exist. */

static void *pp_task_trampoline(void *argument) {
    pp_task *task = (pp_task *)argument;
    ppc_plat_tls_set(g_current_task_key, task);

    const uintptr_t result = task->entry ? task->entry(task->context) : 0;

    ppc_plat_mutex_lock(task->lock);
    task->result = result;
    task->done = 1;
    ppc_plat_mutex_unlock(task->lock);
    return NULL;
}

static void pp_task_release(void *pointer) {
    pp_task *task = (pp_task *)pointer;
    if (task->started && !task->joined) {
        ppc_plat_thread_join(task->thread);
        task->joined = 1;
    }
    if (task->thread) ppc_plat_thread_free(task->thread);
    ppc_plat_mutex_free(task->lock);
    free(task->context);
    free(task);
}

pp_task *pp_task_spawn(pp_task_entry entry, const void *context, int64_t context_size) {
    pp_task *task = (pp_task *)pp_alloc(sizeof(pp_task));
    task->entry = entry;
    task->result = 0;
    task->done = 0;
    task->joined = 0;
    task->cancel_requested = 0;
    task->started = 0;
    task->lock = ppc_plat_mutex_new();
    task->thread = NULL;

    task->context = NULL;
    if (context && context_size > 0) {
        task->context = pp_alloc((size_t)context_size);
        memcpy(task->context, context, (size_t)context_size);
    }
    pp_track(task, pp_task_release);

    task->thread = ppc_plat_thread_start(pp_task_trampoline, task);
    if (!task->thread) {
        /* Falling back to running inline keeps the program correct on a system
         * that refuses new threads; only the concurrency is lost. */
        task->result = entry ? entry(task->context) : 0;
        task->done = 1;
        return task;
    }
    task->started = 1;
    return task;
}

void pp_task_cancel(pp_task *task) {
    if (!task) return;
    ppc_plat_mutex_lock(task->lock);
    task->cancel_requested = 1;
    ppc_plat_mutex_unlock(task->lock);
}

bool pp_task_is_done(pp_task *task) {
    if (!task) return true;
    ppc_plat_mutex_lock(task->lock);
    const int done = task->done;
    ppc_plat_mutex_unlock(task->lock);
    return done != 0;
}

bool pp_task_cancelled(void) {
    pp_task *task = (pp_task *)ppc_plat_tls_get(g_current_task_key);
    if (!task) return false;
    ppc_plat_mutex_lock(task->lock);
    const int requested = task->cancel_requested;
    ppc_plat_mutex_unlock(task->lock);
    return requested != 0;
}

uintptr_t pp_task_await_bits(pp_task *task) {
    if (!task) return 0;
    /* Joining exactly once means a second await returns the same completed
     * result rather than blocking forever. */
    if (task->started && !task->joined) {
        ppc_plat_thread_join(task->thread);
        task->joined = 1;
    }
    return task->result;
}

int64_t pp_task_await_i64(pp_task *task) { return (int64_t)pp_task_await_bits(task); }

double pp_task_await_f64(pp_task *task) {
    const uintptr_t bits = pp_task_await_bits(task);
    double value;
    memcpy(&value, &bits, sizeof(value) < sizeof(bits) ? sizeof(value) : sizeof(bits));
    return value;
}

void *pp_task_await_ptr(pp_task *task) { return (void *)pp_task_await_bits(task); }
void pp_task_await_void(pp_task *task) { (void)pp_task_await_bits(task); }

/* ------------------------------------------------------------------ */
/* Structured task groups                                             */
/*                                                                    */
/* A group holds borrowed references to tasks the runtime already owns */
/* and adds one lifetime boundary around them. Handles are small       */
/* integers indexing a table, so a handle is an ordinary PunPun `int`  */
/* and stays valid even if the table is reallocated.                   */
/* ------------------------------------------------------------------ */

typedef struct {
    pp_task **tasks;
    int64_t count;
    int64_t capacity;
    int closed;
    ppc_plat_mutex *lock;
} pp_task_group;

static pp_task_group **g_groups = NULL;
static int64_t g_group_count = 0;
static int64_t g_group_capacity = 0;

static pp_task_group *pp_group_lookup(int64_t handle) {
    ppc_plat_mutex_lock(g_group_table_lock);
    pp_task_group *group = NULL;
    /* Handles are 1-based so that 0 is never a valid group. */
    if (handle >= 1 && handle <= g_group_count) group = g_groups[handle - 1];
    ppc_plat_mutex_unlock(g_group_table_lock);
    if (!group) {
        char message[96];
        snprintf(message, sizeof(message), "task group handle %" PRId64 " is not valid", handle);
        pp_panic(message);
    }
    return group;
}

static void pp_group_release(void *pointer) {
    pp_task_group *group = (pp_task_group *)pointer;
    ppc_plat_mutex_free(group->lock);
    free(group->tasks);
    free(group);
}

int64_t pp_task_group_new(void) {
    pp_task_group *group = (pp_task_group *)pp_alloc(sizeof(pp_task_group));
    group->tasks = NULL;
    group->count = 0;
    group->capacity = 0;
    group->closed = 0;
    group->lock = ppc_plat_mutex_new();
    pp_track(group, pp_group_release);

    ppc_plat_mutex_lock(g_group_table_lock);
    if (g_group_count == g_group_capacity) {
        const int64_t capacity = g_group_capacity ? g_group_capacity * 2 : 8;
        pp_task_group **table =
            (pp_task_group **)realloc(g_groups, (size_t)capacity * sizeof(pp_task_group *));
        if (!table) {
            ppc_plat_mutex_unlock(g_group_table_lock);
            pp_panic("out of memory creating a task group");
        }
        g_groups = table;
        g_group_capacity = capacity;
    }
    g_groups[g_group_count++] = group;
    const int64_t handle = g_group_count;  /* 1-based */
    ppc_plat_mutex_unlock(g_group_table_lock);
    return handle;
}

void pp_task_group_add(int64_t group_handle, pp_task *task) {
    if (!task) return;
    pp_task_group *group = pp_group_lookup(group_handle);

    ppc_plat_mutex_lock(group->lock);
    if (group->closed) {
        ppc_plat_mutex_unlock(group->lock);
        pp_panic("cannot add a task to a closed group");
    }
    if (group->count == group->capacity) {
        const int64_t capacity = group->capacity ? group->capacity * 2 : 4;
        pp_task **tasks = (pp_task **)realloc(group->tasks, (size_t)capacity * sizeof(pp_task *));
        if (!tasks) {
            ppc_plat_mutex_unlock(group->lock);
            pp_panic("out of memory growing a task group");
        }
        group->tasks = tasks;
        group->capacity = capacity;
    }
    group->tasks[group->count++] = task;
    ppc_plat_mutex_unlock(group->lock);
}

void pp_task_group_cancel(int64_t group_handle) {
    pp_task_group *group = pp_group_lookup(group_handle);
    ppc_plat_mutex_lock(group->lock);
    /* Cancellation is a request, not a kill: each task observes it at its own
     * next safe point. */
    for (int64_t i = 0; i < group->count; ++i) pp_task_cancel(group->tasks[i]);
    ppc_plat_mutex_unlock(group->lock);
}

void pp_task_group_wait(int64_t group_handle) {
    pp_task_group *group = pp_group_lookup(group_handle);
    /* The task list is snapshotted under the lock, then joined outside it, so a
     * worker that adds to the group cannot deadlock against the waiter. */
    for (int64_t i = 0;; ++i) {
        ppc_plat_mutex_lock(group->lock);
        if (i >= group->count) {
            ppc_plat_mutex_unlock(group->lock);
            break;
        }
        pp_task *task = group->tasks[i];
        ppc_plat_mutex_unlock(group->lock);
        pp_task_await_bits(task);
    }
}

bool pp_task_group_wait_for(int64_t group_handle, int64_t timeout_ms) {
    if (timeout_ms < 0) pp_panic("task group timeout must be nonnegative");
    const int64_t deadline = pp_clock_ms() + timeout_ms;

    /* Polling rather than a condition variable keeps this independent of how a
     * task signals completion, and a bounded wait is not on any hot path. */
    for (;;) {
        if (pp_task_group_is_done(group_handle)) {
            /* Join the finished threads so their results are collected. */
            pp_task_group_wait(group_handle);
            return true;
        }
        if (pp_clock_ms() >= deadline) return false;

        ppc_plat_sleep_ms(1);
    }
}

bool pp_task_group_is_done(int64_t group_handle) {
    pp_task_group *group = pp_group_lookup(group_handle);
    ppc_plat_mutex_lock(group->lock);
    bool done = true;
    for (int64_t i = 0; i < group->count; ++i) {
        if (!pp_task_is_done(group->tasks[i])) {
            done = false;
            break;
        }
    }
    ppc_plat_mutex_unlock(group->lock);
    return done;
}

int64_t pp_task_group_pending(int64_t group_handle) {
    pp_task_group *group = pp_group_lookup(group_handle);
    ppc_plat_mutex_lock(group->lock);
    int64_t pending = 0;
    for (int64_t i = 0; i < group->count; ++i) {
        if (!pp_task_is_done(group->tasks[i])) ++pending;
    }
    ppc_plat_mutex_unlock(group->lock);
    return pending;
}

void pp_task_group_close(int64_t group_handle) {
    /* Closing waits first, so no task outlives the group's lifetime boundary.
     * That is the whole point of a group: one place where the caller knows
     * everything it started has finished. */
    pp_task_group_wait(group_handle);
    pp_task_group *group = pp_group_lookup(group_handle);
    ppc_plat_mutex_lock(group->lock);
    group->closed = 1;
    ppc_plat_mutex_unlock(group->lock);
}

/* ------------------------------------------------------------------ */
/* Text operations                                                    */
/*                                                                    */
/* Indices are byte offsets. PunPun str is UTF-8, so a caller doing    */
/* scalar-level work must use pp_utf8_len and friends; these are the   */
/* byte-level primitives everything else is built from.                */
/* ------------------------------------------------------------------ */

int64_t pp_char_at(const char *text, int64_t index) {
    if (!text) pp_panic("char_at on a null string");
    const int64_t length = (int64_t)strlen(text);
    if (index < 0 || index >= length) {
        char message[96];
        snprintf(message, sizeof(message),
                 "string index %" PRId64 " is out of range (length %" PRId64 ")", index, length);
        pp_panic(message);
    }
    /* Unsigned, so a byte above 0x7F reads as 128..255 rather than negative. */
    return (unsigned char)text[index];
}

const char *pp_char_str(int64_t code) {
    if (code < 0 || code > 255) pp_panic("char_str expects a byte value in 0..255");
    if (code == 0) pp_panic("a str cannot contain a NUL byte");
    char *text = (char *)pp_alloc(2);
    text[0] = (char)code;
    text[1] = '\0';
    pp_track(text, NULL);
    return text;
}

int64_t pp_index_of(const char *text, const char *part, int64_t from) {
    if (!text || !part) return -1;
    const int64_t length = (int64_t)strlen(text);
    if (from < 0) from = 0;
    if (from > length) return -1;
    const char *found = strstr(text + from, part);
    return found ? (int64_t)(found - text) : -1;
}

int64_t pp_last_index_of(const char *text, const char *part) {
    if (!text || !part || !*part) return -1;
    int64_t best = -1;
    for (const char *cursor = text; (cursor = strstr(cursor, part)) != NULL; ++cursor) {
        best = (int64_t)(cursor - text);
    }
    return best;
}

bool pp_starts_with(const char *text, const char *prefix) {
    if (!text || !prefix) return false;
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool pp_ends_with(const char *text, const char *suffix) {
    if (!text || !suffix) return false;
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    if (suffix_length > text_length) return false;
    return strcmp(text + text_length - suffix_length, suffix) == 0;
}

/* ASCII-only case conversion. Correct Unicode casing is locale-dependent and
 * needs tables this runtime deliberately does not carry; a text library can
 * build that on top if it needs to. */
static const char *pp_map_case(const char *text, int upper) {
    if (!text) return "";
    const size_t length = strlen(text);
    char *out = (char *)pp_alloc(length + 1);
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (upper && c >= 'a' && c <= 'z') out[i] = (char)(c - 32);
        else if (!upper && c >= 'A' && c <= 'Z') out[i] = (char)(c + 32);
        else out[i] = (char)c;
    }
    out[length] = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_to_upper(const char *text) { return pp_map_case(text, 1); }
const char *pp_to_lower(const char *text) { return pp_map_case(text, 0); }

const char *pp_trim(const char *text) {
    if (!text) return "";
    const char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
    const char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' ||
                           end[-1] == '\r')) {
        --end;
    }
    const size_t length = (size_t)(end - start);
    char *out = (char *)pp_alloc(length + 1);
    memcpy(out, start, length);
    out[length] = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_replace(const char *text, const char *from, const char *to) {
    if (!text) return "";
    if (!from || !*from) return text;
    if (!to) to = "";

    const size_t from_length = strlen(from);
    const size_t to_length = strlen(to);

    /* Count first so the output is one allocation rather than a growing one. */
    size_t occurrences = 0;
    for (const char *cursor = text; (cursor = strstr(cursor, from)) != NULL;
         cursor += from_length) {
        ++occurrences;
    }
    if (occurrences == 0) return text;

    const size_t length = strlen(text) + occurrences * (to_length - from_length);
    char *out = (char *)pp_alloc(length + 1);
    char *write = out;
    for (const char *cursor = text; *cursor;) {
        const char *found = strstr(cursor, from);
        if (!found) {
            const size_t rest = strlen(cursor);
            memcpy(write, cursor, rest);
            write += rest;
            break;
        }
        memcpy(write, cursor, (size_t)(found - cursor));
        write += found - cursor;
        memcpy(write, to, to_length);
        write += to_length;
        cursor = found + from_length;
    }
    *write = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_repeat(const char *text, int64_t times) {
    if (!text || times <= 0) return "";
    const size_t unit = strlen(text);
    /* Guard against an allocation size that would overflow. */
    if (unit != 0 && (uint64_t)times > (uint64_t)(SIZE_MAX - 1) / unit) {
        pp_panic("repeat would produce a string that is too large");
    }
    const size_t length = unit * (size_t)times;
    char *out = (char *)pp_alloc(length + 1);
    for (int64_t i = 0; i < times; ++i) memcpy(out + (size_t)i * unit, text, unit);
    out[length] = '\0';
    pp_track(out, NULL);
    return out;
}

pp_list *pp_split(const char *text, const char *separator) {
    pp_list *parts = pp_list_new();
    if (!text) return parts;
    if (!separator || !*separator) {
        /* An empty separator splits into single bytes, which is what a caller
         * asking for it almost certainly wants. */
        for (const char *c = text; *c; ++c) {
            pp_list_push(parts, (int64_t)(intptr_t)pp_char_str((unsigned char)*c));
        }
        return parts;
    }
    const size_t separator_length = strlen(separator);
    const char *cursor = text;
    for (;;) {
        const char *found = strstr(cursor, separator);
        const size_t length = found ? (size_t)(found - cursor) : strlen(cursor);
        char *piece = (char *)pp_alloc(length + 1);
        memcpy(piece, cursor, length);
        piece[length] = '\0';
        pp_track(piece, NULL);
        pp_list_push(parts, (int64_t)(intptr_t)piece);
        if (!found) break;
        cursor = found + separator_length;
    }
    return parts;
}

const char *pp_join(pp_list *parts, const char *separator) {
    if (!parts) return "";
    if (!separator) separator = "";
    const int64_t count = pp_list_size(parts);
    if (count == 0) return "";

    const size_t separator_length = strlen(separator);
    size_t total = separator_length * (size_t)(count - 1);
    for (int64_t i = 0; i < count; ++i) {
        const char *piece = (const char *)(intptr_t)pp_list_at(parts, i);
        total += piece ? strlen(piece) : 0;
    }

    char *out = (char *)pp_alloc(total + 1);
    char *write = out;
    for (int64_t i = 0; i < count; ++i) {
        if (i) {
            memcpy(write, separator, separator_length);
            write += separator_length;
        }
        const char *piece = (const char *)(intptr_t)pp_list_at(parts, i);
        const size_t length = piece ? strlen(piece) : 0;
        memcpy(write, piece ? piece : "", length);
        write += length;
    }
    *write = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_text_float(double value) {
    char buffer[40];
    /* %.17g round-trips an IEEE double exactly; %g would lose precision. */
    const int written = snprintf(buffer, sizeof(buffer), "%.17g", value);
    char *text = (char *)pp_alloc((size_t)written + 1);
    memcpy(text, buffer, (size_t)written + 1);
    pp_track(text, NULL);
    return text;
}

double pp_parse_float(const char *text) {
    if (!text || !*text) pp_panic("cannot parse an empty string as a float");
    char *stop = NULL;
    const double value = strtod(text, &stop);
    /* Trailing space is tolerated; trailing junk is not, because silently
     * accepting "1.5kg" as 1.5 hides a bug in the caller. */
    while (stop && (*stop == ' ' || *stop == '\t' || *stop == '\n' || *stop == '\r')) ++stop;
    if (stop == text || (stop && *stop)) {
        char message[128];
        snprintf(message, sizeof(message), "cannot parse '%s' as a float", text);
        pp_panic(message);
    }
    return value;
}

static const char *pp_pad(const char *text, int64_t width, const char *fill, int left) {
    if (!text) text = "";
    if (!fill || !*fill) fill = " ";
    const int64_t length = (int64_t)strlen(text);
    if (length >= width) return text;

    const size_t unit = strlen(fill);
    const size_t missing = (size_t)(width - length);
    char *out = (char *)pp_alloc((size_t)length + missing + 1);
    char *write = out;
    if (!left) {
        memcpy(write, text, (size_t)length);
        write += length;
    }
    for (size_t i = 0; i < missing; ++i) write[i] = fill[i % unit];
    write += missing;
    if (left) {
        memcpy(write, text, (size_t)length);
        write += length;
    }
    *write = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_pad_left(const char *text, int64_t width, const char *fill) {
    return pp_pad(text, width, fill, 1);
}
const char *pp_pad_right(const char *text, int64_t width, const char *fill) {
    return pp_pad(text, width, fill, 0);
}

/* ------------------------------------------------------------------ */
/* Math                                                               */
/* ------------------------------------------------------------------ */

double pp_sqrt(double value) {
    if (value < 0.0) pp_panic("sqrt of a negative number");
    return sqrt(value);
}
double pp_pow(double base, double exponent) { return pow(base, exponent); }
double pp_exp(double value) { return exp(value); }
double pp_log(double value) {
    if (value <= 0.0) pp_panic("log of a non-positive number");
    return log(value);
}
double pp_log2(double value) {
    if (value <= 0.0) pp_panic("log2 of a non-positive number");
    return log2(value);
}
double pp_log10(double value) {
    if (value <= 0.0) pp_panic("log10 of a non-positive number");
    return log10(value);
}
double pp_sin(double value) { return sin(value); }
double pp_cos(double value) { return cos(value); }
double pp_tan(double value) { return tan(value); }
double pp_asin(double value) {
    if (value < -1.0 || value > 1.0) pp_panic("asin expects a value in -1..1");
    return asin(value);
}
double pp_acos(double value) {
    if (value < -1.0 || value > 1.0) pp_panic("acos expects a value in -1..1");
    return acos(value);
}
double pp_atan(double value) { return atan(value); }
double pp_atan2(double y, double x) { return atan2(y, x); }
double pp_floor(double value) { return floor(value); }
double pp_ceil(double value) { return ceil(value); }
double pp_round(double value) { return round(value); }
double pp_fabs(double value) { return fabs(value); }
double pp_fmod(double left, double right) {
    if (right == 0.0) pp_panic("float remainder by zero");
    return fmod(left, right);
}
double pp_hypot(double x, double y) { return hypot(x, y); }
bool pp_is_nan(double value) { return value != value; }
bool pp_is_infinite(double value) { return isinf(value) != 0; }

/* ------------------------------------------------------------------ */
/* Pseudorandom                                                       */
/* ------------------------------------------------------------------ */

/* xorshift64*. Small, fast, and good enough for simulation and shuffling.
 * Explicitly not for anything security-related: the state is recoverable from
 * a handful of outputs. pp_random_bytes exists for that. */
static uint64_t g_random_state = 0x853c49e6748fea9bull;

void pp_random_seed(int64_t seed) {
    /* A zero state would make xorshift produce zeros forever. */
    g_random_state = (uint64_t)seed ? (uint64_t)seed : 0x9e3779b97f4a7c15ull;
}

static uint64_t pp_random_next(void) {
    uint64_t x = g_random_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_random_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

int64_t pp_random_int(int64_t low, int64_t high) {
    if (low > high) pp_panic("random_int expects low <= high");
    const uint64_t span = (uint64_t)(high - low) + 1u;
    if (span == 0) return (int64_t)pp_random_next();  /* full 64-bit range */
    /* Rejection sampling: taking the modulus directly would bias the low values
     * whenever the span does not divide 2^64 evenly. */
    const uint64_t limit = UINT64_MAX - (UINT64_MAX % span);
    uint64_t value;
    do {
        value = pp_random_next();
    } while (value >= limit);
    return low + (int64_t)(value % span);
}

double pp_random_float(void) {
    /* 53 bits is exactly the mantissa width, so every result is representable
     * and the distribution is uniform over [0, 1). */
    return (double)(pp_random_next() >> 11) / 9007199254740992.0;
}

/* ------------------------------------------------------------------ */
/* Secure randomness                                                  */
/*                                                                    */
/* Separate from pp_random_* on purpose. The xorshift generator above  */
/* is predictable from its output and must never be used for a key, a  */
/* nonce, a token, or a salt. This one asks the operating system.      */
/* ------------------------------------------------------------------ */

pp_bytes *pp_random_bytes(int64_t count) {
    if (count < 0) pp_panic("random_bytes expects a nonnegative count");
    pp_bytes *out = pp_bytes_new();
    if (count == 0) return out;

    /* Reading the OS entropy device rather than seeding a userspace generator:
     * the kernel's pool is the only source here that is actually unpredictable. */
    FILE *source = fopen("/dev/urandom", "rb");
    if (!source) {
        pp_panic("no source of secure randomness is available on this system");
    }
    for (int64_t i = 0; i < count; ++i) {
        const int byte = fgetc(source);
        if (byte == EOF) {
            fclose(source);
            pp_panic("the system entropy source returned no data");
        }
        pp_bytes_push(out, byte);
    }
    fclose(source);
    return out;
}

/* ------------------------------------------------------------------ */
/* Filesystem                                                         */
/* ------------------------------------------------------------------ */

pp_list *pp_list_dir(const char *path) {
    pp_list *entries = pp_list_new();
    ppc_dir *dir = ppc_plat_dir_open(path);
    if (!dir) {
        char message[256];
        snprintf(message, sizeof(message), "cannot list directory '%s'", path ? path : "");
        pp_panic(message);
    }
    const char *name = NULL;
    ppc_entry_kind kind;
    while (ppc_plat_dir_next(dir, &name, &kind)) {
        const size_t length = strlen(name);
        char *copy = (char *)pp_alloc(length + 1);
        memcpy(copy, name, length + 1);
        pp_track(copy, NULL);
        pp_list_push(entries, (int64_t)(intptr_t)copy);
    }
    ppc_plat_dir_close(dir);
    return entries;
}

bool pp_is_dir(const char *path) { return ppc_plat_entry_kind(path) == PPC_ENTRY_DIRECTORY; }
int64_t pp_file_size_of(const char *path) { return ppc_plat_file_size(path); }
bool pp_remove_dir(const char *path) { return ppc_plat_remove_dir(path); }
bool pp_make_dirs(const char *path) { return ppc_plat_make_dirs(path); }

pp_bytes *pp_read_bytes(const char *path) {
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        char message[256];
        snprintf(message, sizeof(message), "cannot read '%s': %s", path, strerror(errno));
        pp_panic(message);
    }
    pp_bytes *out = pp_bytes_new();
    int byte;
    while ((byte = fgetc(stream)) != EOF) pp_bytes_push(out, byte);
    fclose(stream);
    return out;
}

void pp_write_bytes(const char *path, pp_bytes *bytes) {
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        char message[256];
        snprintf(message, sizeof(message), "cannot write '%s': %s", path, strerror(errno));
        pp_panic(message);
    }
    const int64_t length = pp_bytes_len(bytes);
    for (int64_t i = 0; i < length; ++i) fputc((int)pp_bytes_at(bytes, i), stream);
    if (fclose(stream) != 0) pp_panic("write failed");
}

void pp_append_text(const char *path, const char *text) {
    FILE *stream = fopen(path, "ab");
    if (!stream) {
        char message[256];
        snprintf(message, sizeof(message), "cannot append to '%s': %s", path, strerror(errno));
        pp_panic(message);
    }
    if (text) fputs(text, stream);
    if (fclose(stream) != 0) pp_panic("append failed");
}

/* Path decomposition. Both separators are accepted on input so a path written
 * on one platform still decomposes on the other. */
static int64_t pp_last_separator(const char *path) {
    int64_t best = -1;
    for (int64_t i = 0; path[i]; ++i) {
        if (PPC_IS_SEPARATOR(path[i])) best = i;
    }
    return best;
}

const char *pp_path_parent(const char *path) {
    if (!path) return "";
    const int64_t position = pp_last_separator(path);
    if (position < 0) return ".";
    if (position == 0) return "/";
    char *out = (char *)pp_alloc((size_t)position + 1);
    memcpy(out, path, (size_t)position);
    out[position] = '\0';
    pp_track(out, NULL);
    return out;
}

const char *pp_path_name(const char *path) {
    if (!path) return "";
    const int64_t position = pp_last_separator(path);
    return position < 0 ? path : path + position + 1;
}

const char *pp_path_extension(const char *path) {
    const char *name = pp_path_name(path);
    const char *dot = strrchr(name, '.');
    /* A leading dot is a hidden file, not an extension. */
    return (!dot || dot == name) ? "" : dot + 1;
}

/* ------------------------------------------------------------------ */
/* Time                                                               */
/* ------------------------------------------------------------------ */

int64_t pp_now_ms(void) { return ppc_plat_wall_ms(); }

/* Each accessor breaks the timestamp down independently. Doing it per call is
 * slightly wasteful, but it keeps the PunPun surface a set of plain functions
 * rather than requiring a struct the caller has to thread through. */
static void pp_break_down(int64_t epoch_ms, int *year, int *month, int *day, int *hour,
                          int *minute, int *second) {
    ppc_plat_local_time(epoch_ms, year, month, day, hour, minute, second);
}

int64_t pp_year_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return y;
}
int64_t pp_month_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return mo;
}
int64_t pp_day_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return d;
}
int64_t pp_hour_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return h;
}
int64_t pp_minute_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return mi;
}
int64_t pp_second_of(int64_t epoch_ms) {
    int y, mo, d, h, mi, s;
    pp_break_down(epoch_ms, &y, &mo, &d, &h, &mi, &s);
    return s;
}

int64_t pp_weekday_of(int64_t epoch_ms) {
    /* 1970-01-01 was a Thursday. Floor division, not truncation, so dates
     * before the epoch do not land on the wrong day. */
    int64_t days = epoch_ms / 86400000;
    if (epoch_ms < 0 && epoch_ms % 86400000 != 0) --days;
    int64_t weekday = (days + 4) % 7;
    if (weekday < 0) weekday += 7;
    return weekday;  /* 0 = Sunday */
}

/* ------------------------------------------------------------------ */
/* Process                                                            */
/* ------------------------------------------------------------------ */

void pp_exit(int64_t status) {
    /* Through exit(), not _exit(), so the atexit cleanup still runs and stdout
     * is flushed. */
    fflush(stdout);
    exit((int)status);
}

int64_t pp_run_command(const char *command) {
    if (!command) return -1;
    fflush(stdout);
    const int status = system(command);
    return (int64_t)status;
}
