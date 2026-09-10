/* POSIX implementation of the ppc runtime platform layer.
 *
 * Compiled on every non-Windows target. The whole file is guarded so the build
 * can list both implementations unconditionally and let the preprocessor pick,
 * which keeps the Makefile free of platform conditionals.
 */
/* The feature-test macro must precede every system header, including any that
 * ppc_platform.h pulls in, or clock_gettime and nanosleep stay hidden. */
#define _POSIX_C_SOURCE 200809L

#include "ppc_platform.h"

#if PPC_POSIX

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* The opaque handles from the header are these structs. Allocating them rather
 * than exposing pthread_t directly is what lets the Windows side use a totally
 * different representation without the runtime noticing. */
struct ppc_plat_thread {
    pthread_t handle;
    int joined;
};

struct ppc_plat_mutex {
    pthread_mutex_t handle;
};

struct ppc_plat_tls {
    pthread_key_t key;
};

void ppc_plat_init(void) {
    /* Nothing to do: POSIX terminals are already byte-transparent, so UTF-8
     * output works without setup. */
}

/* -- threads -------------------------------------------------------------- */

ppc_plat_thread *ppc_plat_thread_start(void *(*entry)(void *), void *argument) {
    ppc_plat_thread *thread = (ppc_plat_thread *)malloc(sizeof(ppc_plat_thread));
    if (!thread) return NULL;
    thread->joined = 0;
    if (pthread_create(&thread->handle, NULL, entry, argument) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

void ppc_plat_thread_join(ppc_plat_thread *thread) {
    if (!thread || thread->joined) return;
    pthread_join(thread->handle, NULL);
    thread->joined = 1;
}

void ppc_plat_thread_free(ppc_plat_thread *thread) {
    if (!thread) return;
    /* Joining before free is the caller's contract, but detaching an unjoined
     * thread here prevents leaking its stack if that contract is broken. */
    if (!thread->joined) pthread_detach(thread->handle);
    free(thread);
}

/* -- synchronization ------------------------------------------------------ */

ppc_plat_mutex *ppc_plat_mutex_new(void) {
    ppc_plat_mutex *mutex = (ppc_plat_mutex *)malloc(sizeof(ppc_plat_mutex));
    if (!mutex) return NULL;
    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void ppc_plat_mutex_lock(ppc_plat_mutex *mutex) {
    if (mutex) pthread_mutex_lock(&mutex->handle);
}

void ppc_plat_mutex_unlock(ppc_plat_mutex *mutex) {
    if (mutex) pthread_mutex_unlock(&mutex->handle);
}

void ppc_plat_mutex_free(ppc_plat_mutex *mutex) {
    if (!mutex) return;
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

ppc_plat_tls *ppc_plat_tls_new(void) {
    ppc_plat_tls *tls = (ppc_plat_tls *)malloc(sizeof(ppc_plat_tls));
    if (!tls) return NULL;
    if (pthread_key_create(&tls->key, NULL) != 0) {
        free(tls);
        return NULL;
    }
    return tls;
}

void *ppc_plat_tls_get(ppc_plat_tls *key) {
    return key ? pthread_getspecific(key->key) : NULL;
}

void ppc_plat_tls_set(ppc_plat_tls *key, void *value) {
    if (key) pthread_setspecific(key->key, value);
}

/* -- time ----------------------------------------------------------------- */

int64_t ppc_plat_monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void ppc_plat_sleep_ms(int64_t duration) {
    if (duration <= 0) return;
    struct timespec request;
    request.tv_sec = (time_t)(duration / 1000);
    request.tv_nsec = (long)((duration % 1000) * 1000000L);
    /* Resume the remaining interval after a signal, so an unrelated signal does
     * not shorten the sleep the caller asked for. */
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
    }
}

/* -- filesystem ----------------------------------------------------------- */

struct ppc_dir {
    DIR *handle;
    /* Bounded so that path + '/' + a maximum-length entry name always fits the
     * 4096-byte buffer used when classifying an entry. */
    char path[3072];
};

ppc_entry_kind ppc_plat_entry_kind(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return PPC_ENTRY_MISSING;
    if (S_ISREG(info.st_mode)) return PPC_ENTRY_FILE;
    if (S_ISDIR(info.st_mode)) return PPC_ENTRY_DIRECTORY;
    return PPC_ENTRY_OTHER;
}

int64_t ppc_plat_file_size(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    return (int64_t)info.st_size;
}

bool ppc_plat_remove_dir(const char *path) { return rmdir(path) == 0; }

bool ppc_plat_make_dirs(const char *path) {
    if (!path || !*path) return false;
    if (ppc_plat_entry_kind(path) == PPC_ENTRY_DIRECTORY) return true;

    char buffer[4096];
    const size_t length = strlen(path);
    if (length >= sizeof(buffer)) return false;
    memcpy(buffer, path, length + 1);

    /* Create each prefix in turn. Starting at 1 skips a leading separator, so
     * an absolute path does not try to create the root. */
    for (size_t i = 1; i < length; ++i) {
        if (!PPC_IS_SEPARATOR(buffer[i])) continue;
        buffer[i] = '\0';
        if (*buffer && mkdir(buffer, 0777) != 0 && errno != EEXIST) return false;
        buffer[i] = '/';
    }
    return mkdir(buffer, 0777) == 0 || errno == EEXIST;
}

ppc_dir *ppc_plat_dir_open(const char *path) {
    DIR *handle = opendir(path);
    if (!handle) return NULL;
    ppc_dir *dir = (ppc_dir *)malloc(sizeof(ppc_dir));
    if (!dir) {
        closedir(handle);
        return NULL;
    }
    dir->handle = handle;
    snprintf(dir->path, sizeof(dir->path), "%s", path);
    return dir;
}

bool ppc_plat_dir_next(ppc_dir *dir, const char **name, ppc_entry_kind *kind) {
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir->handle)) != NULL) {
        /* Every caller would otherwise have to filter these out. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        *name = entry->d_name;
        if (kind) {
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", dir->path, entry->d_name);
            *kind = ppc_plat_entry_kind(full);
        }
        return true;
    }
    return false;
}

void ppc_plat_dir_close(ppc_dir *dir) {
    if (!dir) return;
    closedir(dir->handle);
    free(dir);
}

int64_t ppc_plat_wall_ms(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
}

void ppc_plat_local_time(int64_t epoch_ms, int *year, int *month, int *day, int *hour,
                         int *minute, int *second) {
    time_t seconds = (time_t)(epoch_ms / 1000);
    struct tm parts;
    /* The reentrant form: a task on another thread must not clobber this. */
    localtime_r(&seconds, &parts);
    if (year) *year = parts.tm_year + 1900;
    if (month) *month = parts.tm_mon + 1;
    if (day) *day = parts.tm_mday;
    if (hour) *hour = parts.tm_hour;
    if (minute) *minute = parts.tm_min;
    if (second) *second = parts.tm_sec;
}

bool ppc_plat_file_exists(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return false;
    return S_ISREG(info.st_mode) != 0;
}

bool ppc_plat_make_dir(const char *path) { return mkdir(path, 0777) == 0; }
bool ppc_plat_remove_file(const char *path) { return remove(path) == 0; }
bool ppc_plat_rename(const char *from, const char *to) { return rename(from, to) == 0; }

bool ppc_plat_current_dir(char *buffer, size_t size) {
    return getcwd(buffer, size) != NULL;
}

/* -- environment ---------------------------------------------------------- */

const char *ppc_plat_get_env(const char *name) { return getenv(name); }

const char *ppc_plat_name(void) {
#if defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "posix";
#endif
}

#endif /* PPC_POSIX */
