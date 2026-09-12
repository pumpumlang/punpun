/* Windows implementation of the ppc runtime platform layer.
 *
 * STATUS: written but NOT compiled or executed. No Windows toolchain was
 * available in the environment where this was developed, and no mingw
 * cross-compiler could be installed. Treat every function here as unverified
 * until `tools/check-windows.sh` has been run on a machine that has one.
 * The POSIX implementation next to it is exercised by the full test suite; this
 * one has been reviewed, not tested.
 *
 * Design notes worth knowing before editing:
 *
 *  - Threads use _beginthreadex rather than CreateThread. CreateThread does not
 *    initialize the CRT's per-thread state, and the runtime calls CRT functions
 *    (snprintf, malloc) on worker threads, so CreateThread would be a latent
 *    corruption bug rather than an immediate failure.
 *  - CRITICAL_SECTION is used instead of a Win32 mutex object because the
 *    runtime only ever synchronizes within one process, and a critical section
 *    stays in user space in the uncontended case.
 *  - The monotonic clock is QueryPerformanceCounter, not GetTickCount64.
 *    GetTickCount64 has ~16 ms resolution, which is too coarse to measure the
 *    durations PunPun's clock_ms is used for.
 */
#include "ppc_platform.h"

#if PPC_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <direct.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>

struct ppc_plat_thread {
    HANDLE handle;
    int joined;
};

struct ppc_plat_mutex {
    CRITICAL_SECTION section;
};

struct ppc_plat_tls {
    DWORD index;
};

void ppc_plat_init(void) {
    /* Without this the console interprets output in the local ANSI code page,
     * so any non-ASCII PunPun text prints as mojibake. PunPun strings are
     * UTF-8 by definition, so the console has to be told. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

/* -- threads --------------------------------------------------------------
 *
 * The header's entry signature is `void *(*)(void *)` to match POSIX, because
 * that is what the runtime is written against. _beginthreadex wants
 * `unsigned __stdcall (*)(void *)`, so a trampoline adapts between them and the
 * real entry plus its argument travel in a small heap block. */

typedef struct {
    void *(*entry)(void *);
    void *argument;
} ppc_thread_start;

static unsigned __stdcall ppc_thread_trampoline(void *raw) {
    ppc_thread_start start = *(ppc_thread_start *)raw;
    free(raw);
    start.entry(start.argument);
    /* The result is discarded here. The runtime stores a task's result in the
     * task structure before returning, so nothing is lost. */
    return 0;
}

ppc_plat_thread *ppc_plat_thread_start(void *(*entry)(void *), void *argument) {
    ppc_plat_thread *thread = (ppc_plat_thread *)malloc(sizeof(ppc_plat_thread));
    if (!thread) return NULL;

    ppc_thread_start *start = (ppc_thread_start *)malloc(sizeof(ppc_thread_start));
    if (!start) {
        free(thread);
        return NULL;
    }
    start->entry = entry;
    start->argument = argument;

    thread->joined = 0;
    thread->handle = (HANDLE)_beginthreadex(NULL, 0, ppc_thread_trampoline, start, 0, NULL);
    if (thread->handle == NULL) {
        free(start);
        free(thread);
        return NULL;
    }
    return thread;
}

void ppc_plat_thread_join(ppc_plat_thread *thread) {
    if (!thread || thread->joined) return;
    WaitForSingleObject(thread->handle, INFINITE);
    thread->joined = 1;
}

void ppc_plat_thread_free(ppc_plat_thread *thread) {
    if (!thread) return;
    /* The handle must be closed whether or not the thread was joined, or the
     * kernel object leaks for the life of the process. */
    if (thread->handle) CloseHandle(thread->handle);
    free(thread);
}

/* -- synchronization ------------------------------------------------------ */

ppc_plat_mutex *ppc_plat_mutex_new(void) {
    ppc_plat_mutex *mutex = (ppc_plat_mutex *)malloc(sizeof(ppc_plat_mutex));
    if (!mutex) return NULL;
    InitializeCriticalSection(&mutex->section);
    return mutex;
}

void ppc_plat_mutex_lock(ppc_plat_mutex *mutex) {
    if (mutex) EnterCriticalSection(&mutex->section);
}

void ppc_plat_mutex_unlock(ppc_plat_mutex *mutex) {
    if (mutex) LeaveCriticalSection(&mutex->section);
}

void ppc_plat_mutex_free(ppc_plat_mutex *mutex) {
    if (!mutex) return;
    DeleteCriticalSection(&mutex->section);
    free(mutex);
}

ppc_plat_tls *ppc_plat_tls_new(void) {
    ppc_plat_tls *tls = (ppc_plat_tls *)malloc(sizeof(ppc_plat_tls));
    if (!tls) return NULL;
    tls->index = TlsAlloc();
    if (tls->index == TLS_OUT_OF_INDEXES) {
        free(tls);
        return NULL;
    }
    return tls;
}

void *ppc_plat_tls_get(ppc_plat_tls *key) {
    return key ? TlsGetValue(key->index) : NULL;
}

void ppc_plat_tls_set(ppc_plat_tls *key, void *value) {
    if (key) TlsSetValue(key->index, value);
}

/* -- time ----------------------------------------------------------------- */

int64_t ppc_plat_monotonic_ms(void) {
    /* The frequency is fixed for the life of the process, so it is queried
     * once. QueryPerformanceCounter is monotonic on every supported Windows
     * version and has sub-microsecond resolution. */
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (frequency.QuadPart == 0) return 0;
    /* Divide before multiplying would lose all precision; multiplying first
     * would overflow after a few weeks of uptime. Split the counter instead. */
    const int64_t seconds = now.QuadPart / frequency.QuadPart;
    const int64_t remainder = now.QuadPart % frequency.QuadPart;
    return seconds * 1000 + (remainder * 1000) / frequency.QuadPart;
}

void ppc_plat_sleep_ms(int64_t duration) {
    if (duration <= 0) return;
    /* Sleep takes a DWORD, so a very long request is clamped. The caller loops
     * anyway, both to honor the full duration and to poll for cancellation. */
    while (duration > 0) {
        const DWORD chunk = duration > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)duration;
        Sleep(chunk);
        duration -= (int64_t)chunk;
    }
}

/* -- filesystem ----------------------------------------------------------- */

struct ppc_dir {
    HANDLE handle;
    WIN32_FIND_DATAA entry;
    int first;      /* FindFirstFile already produced an entry */
    int exhausted;
};

ppc_entry_kind ppc_plat_entry_kind(const char *path) {
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return PPC_ENTRY_MISSING;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return PPC_ENTRY_DIRECTORY;
    return PPC_ENTRY_FILE;
}

int64_t ppc_plat_file_size(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    /* The size is split across two DWORDs; recombining is not optional. */
    return ((int64_t)data.nFileSizeHigh << 32) | (int64_t)data.nFileSizeLow;
}

bool ppc_plat_remove_dir(const char *path) { return RemoveDirectoryA(path) != 0; }

bool ppc_plat_make_dirs(const char *path) {
    if (!path || !*path) return false;
    if (ppc_plat_entry_kind(path) == PPC_ENTRY_DIRECTORY) return true;

    char buffer[4096];
    const size_t length = strlen(path);
    if (length >= sizeof(buffer)) return false;
    memcpy(buffer, path, length + 1);

    /* Start at 3 rather than 1 so a drive prefix like "C:\" is not treated as a
     * component to create. */
    for (size_t i = (length > 2 && buffer[1] == ':') ? 3 : 1; i < length; ++i) {
        if (!PPC_IS_SEPARATOR(buffer[i])) continue;
        const char saved = buffer[i];
        buffer[i] = '\0';
        if (*buffer && !CreateDirectoryA(buffer, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        buffer[i] = saved;
    }
    return CreateDirectoryA(buffer, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

ppc_dir *ppc_plat_dir_open(const char *path) {
    char pattern[4096];
    /* FindFirstFile needs a wildcard, unlike opendir which takes the directory. */
    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern)) {
        return NULL;
    }
    ppc_dir *dir = (ppc_dir *)malloc(sizeof(ppc_dir));
    if (!dir) return NULL;
    dir->handle = FindFirstFileA(pattern, &dir->entry);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    /* FindFirstFile already returned the first entry, so the first call to
     * dir_next must consume it rather than advancing past it. */
    dir->first = 1;
    dir->exhausted = 0;
    return dir;
}

bool ppc_plat_dir_next(ppc_dir *dir, const char **name, ppc_entry_kind *kind) {
    if (!dir || dir->exhausted) return false;
    for (;;) {
        if (!dir->first) {
            if (!FindNextFileA(dir->handle, &dir->entry)) {
                dir->exhausted = 1;
                return false;
            }
        }
        dir->first = 0;

        if (strcmp(dir->entry.cFileName, ".") == 0 ||
            strcmp(dir->entry.cFileName, "..") == 0) {
            continue;
        }
        *name = dir->entry.cFileName;
        if (kind) {
            *kind = (dir->entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        ? PPC_ENTRY_DIRECTORY
                        : PPC_ENTRY_FILE;
        }
        return true;
    }
}

void ppc_plat_dir_close(ppc_dir *dir) {
    if (!dir) return;
    if (dir->handle != INVALID_HANDLE_VALUE) FindClose(dir->handle);
    free(dir);
}

int64_t ppc_plat_wall_ms(void) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    /* FILETIME counts 100-nanosecond ticks from 1601-01-01; the constant is the
     * offset to the Unix epoch. */
    ULARGE_INTEGER ticks;
    ticks.LowPart = now.dwLowDateTime;
    ticks.HighPart = now.dwHighDateTime;
    return (int64_t)((ticks.QuadPart - 116444736000000000ull) / 10000ull);
}

void ppc_plat_local_time(int64_t epoch_ms, int *year, int *month, int *day, int *hour,
                         int *minute, int *second) {
    const __time64_t seconds = (__time64_t)(epoch_ms / 1000);
    struct tm parts;
    /* localtime_s, not localtime: the latter returns shared storage that a task
     * on another thread could overwrite mid-read. */
    if (_localtime64_s(&parts, &seconds) != 0) {
        memset(&parts, 0, sizeof(parts));
    }
    if (year) *year = parts.tm_year + 1900;
    if (month) *month = parts.tm_mon + 1;
    if (day) *day = parts.tm_mday;
    if (hour) *hour = parts.tm_hour;
    if (minute) *minute = parts.tm_min;
    if (second) *second = parts.tm_sec;
}

bool ppc_plat_file_exists(const char *path) {
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    /* Matches the POSIX side, which reports only regular files. */
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ppc_plat_make_dir(const char *path) {
    return CreateDirectoryA(path, NULL) != 0;
}

bool ppc_plat_remove_file(const char *path) {
    return DeleteFileA(path) != 0;
}

bool ppc_plat_rename(const char *from, const char *to) {
    /* POSIX rename replaces an existing destination; plain MoveFileA does not.
     * MOVEFILE_REPLACE_EXISTING makes the two platforms agree, which matters
     * because the compiler's cache relies on rename-over being atomic. */
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
}

bool ppc_plat_current_dir(char *buffer, size_t size) {
    const DWORD written = GetCurrentDirectoryA((DWORD)size, buffer);
    return written != 0 && written < size;
}

/* -- environment ---------------------------------------------------------- */

const char *ppc_plat_get_env(const char *name) { return getenv(name); }

bool ppc_plat_set_env(const char *name, const char *value) {
    if (!name || !*name) return false;
    return _putenv_s(name, value ? value : "") == 0;
}

bool ppc_plat_hostname(char *buffer, size_t size) {
    if (!buffer || size == 0) return false;
    DWORD length = (DWORD)size;
    return GetComputerNameA(buffer, &length) != 0;
}

int64_t ppc_plat_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors ? (int64_t)info.dwNumberOfProcessors : 1;
}

int64_t ppc_plat_run_capture(const char *command, char **output) {
    if (output) *output = NULL;
    if (!command || !output) return -1;
    FILE *pipe = _popen(command, "r");
    if (!pipe) return -1;
    size_t capacity = 4096, length = 0;
    char *data = (char *)malloc(capacity);
    if (!data) { _pclose(pipe); return -1; }
    char chunk[2048];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        const size_t n = strlen(chunk);
        if (length + n + 1 > capacity) {
            while (length + n + 1 > capacity) capacity *= 2;
            char *grown = (char *)realloc(data, capacity);
            if (!grown) { free(data); _pclose(pipe); return -1; }
            data = grown;
        }
        memcpy(data + length, chunk, n); length += n;
    }
    data[length] = '\0';
    const int status = _pclose(pipe);
    *output = data;
    return (int64_t)status;
}

const char *ppc_plat_name(void) { return "windows"; }

#endif /* PPC_WINDOWS */
