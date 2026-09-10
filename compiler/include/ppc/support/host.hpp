#ifndef PPC_SUPPORT_HOST_HPP
#define PPC_SUPPORT_HOST_HPP

#include <string>
#include <vector>

#include "ppc/support/common.hpp"

namespace ppc {

/// Everything the compiler driver needs from the operating system.
///
/// This is the compiler's platform boundary, distinct from runtime/ppc_platform.h
/// which serves compiled programs. It exists so that porting PPC to another host
/// means implementing this one interface rather than hunting `/proc/self/exe`,
/// `/tmp`, and `fork` through the driver.
///
/// The interface names capabilities, never a platform's spelling of them:
/// `create_temp_file` rather than `mkstemp`.
namespace host {

/// Absolute path of the running compiler binary. Empty when it cannot be
/// determined, which callers must treat as "search elsewhere" rather than fatal.
std::string executable_path();

/// Directory for temporary files, honoring the platform's conventions and the
/// usual environment overrides. No trailing separator.
std::string temp_directory();

/// Per-user cache directory for PPC, following platform convention.
std::string cache_directory();

/// A uniquely named temporary file, created atomically and owner-only.
///
/// Returns the path, or empty on failure. Using a predictable name such as
/// `/tmp/ppc-<pid>.c` is unsafe: process ids recycle, two concurrent builds
/// collide, and on a shared /tmp another user can pre-create the path and win
/// the race. This creates the file exclusively so neither can happen.
std::string create_temp_file(const std::string &prefix, const std::string &suffix);

/// A uniquely named temporary directory, created owner-only.
std::string create_temp_directory(const std::string &prefix);

/// Deletes a file, ignoring absence. Returns true when the path is gone after.
bool remove_file(const std::string &path);

/// Renames `from` over `to`, replacing any existing file.
///
/// The cache relies on this being atomic within a filesystem: two concurrent
/// builds may compile the same object, and neither may ever observe a
/// half-written file. POSIX rename replaces silently; Windows MoveFileEx needs
/// MOVEFILE_REPLACE_EXISTING to behave the same way.
bool rename_file(const std::string &from, const std::string &to);
/// Deletes a directory and everything in it.
bool remove_directory_tree(const std::string &path);

/// Creates a directory and any missing parents.
bool make_directories(const std::string &path);
bool directory_exists(const std::string &path);

/// Runs a program and waits. Returns its exit status, 128+signal if it was
/// killed, or -1 if it could not be started. `argv[0]` is the program.
///
/// Deliberately not `system()`: arguments go to the program exactly as given,
/// with no shell to reinterpret quotes, spaces, or metacharacters in a path.
int run_process(const std::vector<std::string> &argv, bool echo);

/// True when the given standard stream is a terminal (1 = stdout, 2 = stderr).
bool is_terminal(int stream);

/// Target triple components for the host, used in cache identity so an artifact
/// built for one target is never reused for another.
const char *platform_name();
const char *architecture_name();

/// Identity of a C toolchain: its reported version banner, or empty if the
/// program cannot be run. Part of cache identity, because the same command name
/// can resolve to a different compiler after an upgrade.
std::string toolchain_identity(const std::string &program);

/// Native suffix for an executable, "" on POSIX and ".exe" on Windows.
const char *executable_suffix();

}  // namespace host
}  // namespace ppc

#endif
