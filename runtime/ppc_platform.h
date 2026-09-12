/* ppc runtime — platform abstraction.
 *
 * Everything the runtime needs from the operating system goes through here, so
 * ppcrt.c contains no #ifdef at all. Exactly two implementations sit behind it:
 * ppc_platform_posix.c and ppc_platform_windows.c, and exactly one is compiled.
 *
 * Scope rule: this header contains only capabilities ppcrt.c actually calls.
 * An abstraction full of unused entry points is a maintenance cost with no
 * user, and every unused function is one nobody notices is broken.
 *
 * Naming rule: expose the capability, never the platform's spelling of it.
 * `ppc_plat_monotonic_ms` is right; `ppc_plat_clock_gettime` would not be,
 * because it names a POSIX function and would force the Windows side to
 * emulate an interface it does not have.
 */
#ifndef PPC_PLATFORM_H
#define PPC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -- platform detection -------------------------------------------------- */

#if defined(_WIN32) || defined(_WIN64)
#  define PPC_WINDOWS 1
#  define PPC_POSIX 0
#  define PPC_PATH_SEPARATOR '\\'
#  define PPC_EXECUTABLE_SUFFIX ".exe"
#else
#  define PPC_WINDOWS 0
#  define PPC_POSIX 1
#  define PPC_PATH_SEPARATOR '/'
#  define PPC_EXECUTABLE_SUFFIX ""
#endif

/* Both separators are accepted on input everywhere; only the native one is
 * produced. A PunPun program written on Linux with "a/b" paths therefore still
 * runs unchanged on Windows. */
#define PPC_IS_SEPARATOR(c) ((c) == '/' || (c) == '\\')

#ifdef __cplusplus
extern "C" {
#endif

/* -- lifecycle ------------------------------------------------------------
 * Called once from pp_runtime_init, before anything else. On Windows this
 * switches the console code page to UTF-8, without which PunPun text prints as
 * mojibake; on POSIX it does nothing. */
void ppc_plat_init(void);

/* -- threads --------------------------------------------------------------
 * Opaque handles rather than typedefs of the native types, so no caller can
 * accidentally depend on one platform's representation. */
typedef struct ppc_plat_thread ppc_plat_thread;
typedef struct ppc_plat_mutex ppc_plat_mutex;
typedef struct ppc_plat_tls ppc_plat_tls;

/* Returns NULL when a thread cannot be started. Callers must fall back to
 * running the work inline rather than failing: losing concurrency is
 * acceptable, losing the result is not. */
ppc_plat_thread *ppc_plat_thread_start(void *(*entry)(void *), void *argument);
void ppc_plat_thread_join(ppc_plat_thread *thread);
/* Releases the handle. Must be preceded by a join. */
void ppc_plat_thread_free(ppc_plat_thread *thread);

ppc_plat_mutex *ppc_plat_mutex_new(void);
void ppc_plat_mutex_lock(ppc_plat_mutex *mutex);
void ppc_plat_mutex_unlock(ppc_plat_mutex *mutex);
void ppc_plat_mutex_free(ppc_plat_mutex *mutex);

ppc_plat_tls *ppc_plat_tls_new(void);
void *ppc_plat_tls_get(ppc_plat_tls *key);
void ppc_plat_tls_set(ppc_plat_tls *key, void *value);

/* -- time ----------------------------------------------------------------- */

/* Milliseconds from an arbitrary origin that never moves backwards, so it is
 * the correct clock for measuring a duration. Wall-clock time is deliberately
 * not exposed: the runtime has no use for it and it would invite misuse. */
int64_t ppc_plat_monotonic_ms(void);

/* Milliseconds since the Unix epoch, for a timestamp a human will read. Unlike
 * the monotonic clock this can jump, so it must not be used to measure a
 * duration. */
int64_t ppc_plat_wall_ms(void);

/* Local-time breakdown of a Unix-epoch millisecond value. Month is 1..12 and
 * day is 1..31, matching how a person writes a date rather than how C stores
 * one. */
void ppc_plat_local_time(int64_t epoch_ms, int *year, int *month, int *day, int *hour,
                         int *minute, int *second);

/* Sleeps for at most `duration` milliseconds. May return early; the caller
 * loops, which is also what makes sleeping a cancellation safe point. */
void ppc_plat_sleep_ms(int64_t duration);

/* -- filesystem -----------------------------------------------------------
 * Only what the PunPun fs builtins expose. Anything richer belongs in the
 * standard library, not here. */

typedef enum {
    PPC_ENTRY_MISSING = 0,
    PPC_ENTRY_FILE = 1,
    PPC_ENTRY_DIRECTORY = 2,
    PPC_ENTRY_OTHER = 3,
} ppc_entry_kind;

ppc_entry_kind ppc_plat_entry_kind(const char *path);
/* -1 when the size cannot be determined. */
int64_t ppc_plat_file_size(const char *path);
bool ppc_plat_remove_dir(const char *path);
/* Creates every missing component, like `mkdir -p`. */
bool ppc_plat_make_dirs(const char *path);

/* Directory iteration. `ppc_plat_dir_next` returns false when exhausted; the
 * name it yields is valid only until the next call. `.` and `..` are skipped,
 * because every caller has to filter them out otherwise. */
typedef struct ppc_dir ppc_dir;
ppc_dir *ppc_plat_dir_open(const char *path);
bool ppc_plat_dir_next(ppc_dir *dir, const char **name, ppc_entry_kind *kind);
void ppc_plat_dir_close(ppc_dir *dir);

bool ppc_plat_file_exists(const char *path);
bool ppc_plat_make_dir(const char *path);
bool ppc_plat_remove_file(const char *path);
bool ppc_plat_rename(const char *from, const char *to);
/* Writes the working directory into `buffer`; false when it does not fit. */
bool ppc_plat_current_dir(char *buffer, size_t size);

/* -- environment ---------------------------------------------------------- */

const char *ppc_plat_get_env(const char *name);
bool ppc_plat_set_env(const char *name, const char *value);
bool ppc_plat_hostname(char *buffer, size_t size);
int64_t ppc_plat_cpu_count(void);
/* Runs through the host shell and captures stdout. `*output` is malloc-owned by
 * the caller. The return value is the normalized process exit status. */
int64_t ppc_plat_run_capture(const char *command, char **output);
/* Compile-time platform name, matching what `platform()` returns to PunPun. */
const char *ppc_plat_name(void);

#ifdef __cplusplus
}
#endif

#endif
