/* Secure HTTPS support for PunPun.
 *
 * libcurl is loaded at runtime so PPC itself remains bootstrap-able with only
 * a C/C++ toolchain. Requests are HTTPS-only, certificate and host verification
 * stay enabled, and redirects are restricted to HTTPS.
 */
#include "ppcrt.h"
#include "ppc_platform.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PPC_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

typedef void CURL;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;
struct curl_slist;

enum {
    PP_CURLE_OK = 0,
    PP_CURLOPT_WRITEDATA = 10001,
    PP_CURLOPT_URL = 10002,
    PP_CURLOPT_POSTFIELDS = 10015,
    PP_CURLOPT_USERAGENT = 10018,
    PP_CURLOPT_HTTPHEADER = 10023,
    PP_CURLOPT_WRITEFUNCTION = 20011,
    PP_CURLOPT_CUSTOMREQUEST = 10036,
    PP_CURLOPT_NOBODY = 44,
    PP_CURLOPT_POST = 47,
    PP_CURLOPT_FOLLOWLOCATION = 52,
    PP_CURLOPT_SSL_VERIFYPEER = 64,
    PP_CURLOPT_MAXREDIRS = 68,
    PP_CURLOPT_SSL_VERIFYHOST = 81,
    PP_CURLOPT_NOSIGNAL = 99,
    PP_CURLOPT_TIMEOUT_MS = 155,
    PP_CURLOPT_CONNECTTIMEOUT_MS = 156,
    PP_CURLOPT_PROTOCOLS = 181,
    PP_CURLOPT_REDIR_PROTOCOLS = 182,
    PP_CURLINFO_RESPONSE_CODE = 0x200002,
    PP_CURL_GLOBAL_DEFAULT = 3,
    PP_CURLPROTO_HTTPS = 1 << 1
};

typedef CURL *(*pp_curl_easy_init_fn)(void);
typedef CURLcode (*pp_curl_easy_setopt_fn)(CURL *, CURLoption, ...);
typedef CURLcode (*pp_curl_easy_perform_fn)(CURL *);
typedef CURLcode (*pp_curl_easy_getinfo_fn)(CURL *, CURLINFO, ...);
typedef void (*pp_curl_easy_cleanup_fn)(CURL *);
typedef const char *(*pp_curl_easy_strerror_fn)(CURLcode);
typedef CURLcode (*pp_curl_global_init_fn)(long);
typedef struct curl_slist *(*pp_curl_slist_append_fn)(struct curl_slist *, const char *);
typedef void (*pp_curl_slist_free_all_fn)(struct curl_slist *);

static void *g_curl_library;
static pp_curl_easy_init_fn g_easy_init;
static pp_curl_easy_setopt_fn g_easy_setopt;
static pp_curl_easy_perform_fn g_easy_perform;
static pp_curl_easy_getinfo_fn g_easy_getinfo;
static pp_curl_easy_cleanup_fn g_easy_cleanup;
static pp_curl_easy_strerror_fn g_easy_strerror;
static pp_curl_global_init_fn g_global_init;
static pp_curl_slist_append_fn g_slist_append;
static pp_curl_slist_free_all_fn g_slist_free_all;
static int g_https_initialized;
static int g_https_available;
static char g_https_load_error[256];

#if defined(_MSC_VER)
#  define PP_THREAD_LOCAL __declspec(thread)
#else
#  define PP_THREAD_LOCAL _Thread_local
#endif

static PP_THREAD_LOCAL int64_t g_https_status;
static PP_THREAD_LOCAL char g_https_error[512];

static void *pp_https_open_library(void) {
#if PPC_WINDOWS
    static const char *names[] = {"libcurl-x64.dll", "libcurl.dll", "curl.dll", NULL};
    for (int i = 0; names[i]; ++i) {
        HMODULE module = LoadLibraryA(names[i]);
        if (module) return (void *)module;
    }
#else
    static const char *names[] = {"libcurl.so.4", "libcurl.so", "libcurl.dylib", NULL};
    for (int i = 0; names[i]; ++i) {
        void *module = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (module) return module;
    }
#endif
    return NULL;
}

static void *pp_https_symbol(const char *name) {
#if PPC_WINDOWS
    return (void *)GetProcAddress((HMODULE)g_curl_library, name);
#else
    return dlsym(g_curl_library, name);
#endif
}

static int pp_https_bind(void **slot, const char *name) {
    *slot = pp_https_symbol(name);
    if (*slot) return 1;
    snprintf(g_https_load_error, sizeof(g_https_load_error),
             "libcurl symbol '%s' is unavailable", name);
    return 0;
}

void pp_https_runtime_init(void) {
    if (g_https_initialized) return;
    g_https_initialized = 1;
    g_curl_library = pp_https_open_library();
    if (!g_curl_library) {
        snprintf(g_https_load_error, sizeof(g_https_load_error),
                 "libcurl is not installed or could not be loaded");
        return;
    }

#define PP_BIND(target, name) \
    do { if (!pp_https_bind((void **)&(target), (name))) return; } while (0)
    PP_BIND(g_easy_init, "curl_easy_init");
    PP_BIND(g_easy_setopt, "curl_easy_setopt");
    PP_BIND(g_easy_perform, "curl_easy_perform");
    PP_BIND(g_easy_getinfo, "curl_easy_getinfo");
    PP_BIND(g_easy_cleanup, "curl_easy_cleanup");
    PP_BIND(g_easy_strerror, "curl_easy_strerror");
    PP_BIND(g_global_init, "curl_global_init");
    PP_BIND(g_slist_append, "curl_slist_append");
    PP_BIND(g_slist_free_all, "curl_slist_free_all");
#undef PP_BIND

    if (g_global_init(PP_CURL_GLOBAL_DEFAULT) != PP_CURLE_OK) {
        snprintf(g_https_load_error, sizeof(g_https_load_error),
                 "libcurl global initialization failed");
        return;
    }
    g_https_available = 1;
}

bool pp_https_available(void) {
    if (!g_https_initialized) pp_https_runtime_init();
    return g_https_available != 0;
}

typedef struct pp_https_buffer {
    char *data;
    size_t length;
    int exceeded_limit;
} pp_https_buffer;

static size_t pp_https_write(char *data, size_t size, size_t count, void *context) {
    pp_https_buffer *buffer = (pp_https_buffer *)context;
    if (size != 0u && count > SIZE_MAX / size) {
        buffer->exceeded_limit = 1;
        return 0;
    }
    const size_t bytes = size * count;
    const size_t maximum = 64u * 1024u * 1024u;
    if (bytes > maximum - buffer->length) {
        buffer->exceeded_limit = 1;
        return 0;
    }
    char *next = (char *)realloc(buffer->data, buffer->length + bytes + 1u);
    if (!next) return 0;
    buffer->data = next;
    memcpy(buffer->data + buffer->length, data, bytes);
    buffer->length += bytes;
    buffer->data[buffer->length] = '\0';
    return bytes;
}

static int pp_https_method_valid(const char *method) {
    if (!method || !*method) return 0;
    size_t length = 0;
    for (; method[length]; ++length) {
        const unsigned char ch = (unsigned char)method[length];
        if ((!isalpha(ch) && ch != '-') || length >= 31u) return 0;
    }
    return length > 0u;
}

static struct curl_slist *pp_https_headers(const char *headers) {
    if (!headers || !*headers) return NULL;
    const size_t length = strlen(headers);
    char *copy = (char *)malloc(length + 1u);
    if (!copy) return NULL;
    memcpy(copy, headers, length + 1u);
    struct curl_slist *list = NULL;
    char *line = copy;
    for (char *cursor = copy;; ++cursor) {
        if (*cursor != '\n' && *cursor != '\0') continue;
        const int done = *cursor == '\0';
        *cursor = '\0';
        size_t line_length = strlen(line);
        if (line_length && line[line_length - 1u] == '\r') line[line_length - 1u] = '\0';
        if (*line) {
            struct curl_slist *next = g_slist_append(list, line);
            if (!next) {
                if (list) g_slist_free_all(list);
                free(copy);
                return NULL;
            }
            list = next;
        }
        if (done) break;
        line = cursor + 1;
    }
    free(copy);
    return list;
}

const char *pp_https_request(const char *method, const char *url, const char *body,
                             const char *headers, int64_t timeout_ms, bool follow_redirects) {
    g_https_status = 0;
    g_https_error[0] = '\0';
    if (!url || strncmp(url, "https://", 8) != 0) {
        snprintf(g_https_error, sizeof(g_https_error), "HTTPS URL must begin with https://");
        return "";
    }
    if (!pp_https_method_valid(method)) {
        snprintf(g_https_error, sizeof(g_https_error), "invalid HTTPS method");
        return "";
    }
    if (timeout_ms <= 0 || timeout_ms > 3600000) {
        snprintf(g_https_error, sizeof(g_https_error),
                 "HTTPS timeout must be between 1 and 3600000 milliseconds");
        return "";
    }
    if (!pp_https_available()) {
        snprintf(g_https_error, sizeof(g_https_error), "%s", g_https_load_error);
        return "";
    }

    CURL *curl = g_easy_init();
    if (!curl) {
        snprintf(g_https_error, sizeof(g_https_error), "curl_easy_init failed");
        return "";
    }
    pp_https_buffer buffer = {NULL, 0u, 0};
    struct curl_slist *header_list = pp_https_headers(headers);
    if (headers && *headers && !header_list) {
        g_easy_cleanup(curl);
        snprintf(g_https_error, sizeof(g_https_error), "cannot allocate HTTPS headers");
        return "";
    }

    g_easy_setopt(curl, PP_CURLOPT_URL, url);
    g_easy_setopt(curl, PP_CURLOPT_NOSIGNAL, 1L);
    g_easy_setopt(curl, PP_CURLOPT_SSL_VERIFYPEER, 1L);
    g_easy_setopt(curl, PP_CURLOPT_SSL_VERIFYHOST, 2L);
    g_easy_setopt(curl, PP_CURLOPT_PROTOCOLS, (long)PP_CURLPROTO_HTTPS);
    g_easy_setopt(curl, PP_CURLOPT_REDIR_PROTOCOLS, (long)PP_CURLPROTO_HTTPS);
    g_easy_setopt(curl, PP_CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
    g_easy_setopt(curl, PP_CURLOPT_MAXREDIRS, 10L);
    g_easy_setopt(curl, PP_CURLOPT_CONNECTTIMEOUT_MS, (long)(timeout_ms < 10000 ? timeout_ms : 10000));
    g_easy_setopt(curl, PP_CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    g_easy_setopt(curl, PP_CURLOPT_USERAGENT, "PunPun-HTTPS/1.3");
    g_easy_setopt(curl, PP_CURLOPT_WRITEFUNCTION, pp_https_write);
    g_easy_setopt(curl, PP_CURLOPT_WRITEDATA, &buffer);
    if (header_list) g_easy_setopt(curl, PP_CURLOPT_HTTPHEADER, header_list);

    if (strcmp(method, "HEAD") == 0) {
        g_easy_setopt(curl, PP_CURLOPT_NOBODY, 1L);
    } else if (strcmp(method, "POST") == 0) {
        g_easy_setopt(curl, PP_CURLOPT_POST, 1L);
        g_easy_setopt(curl, PP_CURLOPT_POSTFIELDS, body ? body : "");
    } else if (strcmp(method, "GET") != 0) {
        g_easy_setopt(curl, PP_CURLOPT_CUSTOMREQUEST, method);
        if (body && *body) g_easy_setopt(curl, PP_CURLOPT_POSTFIELDS, body);
    }

    const CURLcode code = g_easy_perform(curl);
    if (code == PP_CURLE_OK) {
        (void)g_easy_getinfo(curl, PP_CURLINFO_RESPONSE_CODE, &g_https_status);
    } else if (buffer.exceeded_limit) {
        snprintf(g_https_error, sizeof(g_https_error), "HTTPS response exceeded 64 MiB");
    } else {
        snprintf(g_https_error, sizeof(g_https_error), "%s", g_easy_strerror(code));
    }

    if (header_list) g_slist_free_all(header_list);
    g_easy_cleanup(curl);
    if (code != PP_CURLE_OK) {
        free(buffer.data);
        return "";
    }
    if (!buffer.data) {
        buffer.data = (char *)malloc(1u);
        if (!buffer.data) {
            snprintf(g_https_error, sizeof(g_https_error), "out of memory storing HTTPS response");
            return "";
        }
        buffer.data[0] = '\0';
    }
    return pp_adopt_text(buffer.data);
}

int64_t pp_https_status(void) { return g_https_status; }

const char *pp_https_error(void) {
    if (g_https_error[0]) return g_https_error;
    return g_https_available ? "" : g_https_load_error;
}
