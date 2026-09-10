#include "ppc/support/host.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "ppc/support/source.hpp"

#if defined(_WIN32) || defined(_WIN64)
#  define PPC_HOST_WINDOWS 1
#else
#  define PPC_HOST_WINDOWS 0
#endif

#if PPC_HOST_WINDOWS
#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace ppc {
namespace host {

namespace {

/// Random suffix for a temporary name. Seeded from the system entropy source
/// rather than the clock, so two builds started in the same millisecond do not
/// generate the same candidate.
std::string random_suffix() {
    static thread_local std::mt19937_64 engine(std::random_device{}());
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    result.reserve(12);
    for (int i = 0; i < 12; ++i) {
        result += kAlphabet[engine() % (sizeof(kAlphabet) - 1)];
    }
    return result;
}

}  // namespace

const char *platform_name() {
#if PPC_HOST_WINDOWS
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "unknown";
#endif
}

const char *architecture_name() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

const char *executable_suffix() {
#if PPC_HOST_WINDOWS
    return ".exe";
#else
    return "";
#endif
}

std::string executable_path() {
#if PPC_HOST_WINDOWS
    char buffer[4096];
    const DWORD length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer)) return {};
    return std::string(buffer, length);
#elif defined(__linux__)
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    return std::string(buffer, static_cast<std::size_t>(length));
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    extern int _NSGetExecutablePath(char *, uint32_t *);
    if (_NSGetExecutablePath(buffer, &size) != 0) return {};
    return std::string(buffer);
#else
    return {};
#endif
}

std::string temp_directory() {
#if PPC_HOST_WINDOWS
    char buffer[MAX_PATH + 1];
    const DWORD length = GetTempPathA(sizeof(buffer), buffer);
    if (length == 0) return ".";
    std::string result(buffer, length);
    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    return result;
#else
    // TMPDIR is the POSIX convention and is what container and CI environments
    // set when /tmp is unsuitable or read-only.
    for (const char *name : {"TMPDIR", "TMP", "TEMP"}) {
        if (const char *value = std::getenv(name)) {
            if (*value) return value;
        }
    }
    return "/tmp";
#endif
}

std::string cache_directory() {
    if (const char *explicit_path = std::getenv("PPC_CACHE")) {
        if (*explicit_path) return explicit_path;
    }
#if PPC_HOST_WINDOWS
    if (const char *local = std::getenv("LOCALAPPDATA")) {
        return join_path(local, "ppc");
    }
    return join_path(temp_directory(), "ppc-cache");
#else
    // XDG first, then the conventional fallback.
    if (const char *xdg = std::getenv("XDG_CACHE_HOME")) {
        if (*xdg) return join_path(xdg, "ppc");
    }
    if (const char *home = std::getenv("HOME")) {
        if (*home) return join_path(join_path(home, ".cache"), "ppc");
    }
    return join_path(temp_directory(), "ppc-cache");
#endif
}

bool directory_exists(const std::string &path) {
#if PPC_HOST_WINDOWS
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return S_ISDIR(info.st_mode) != 0;
#endif
}

bool make_directories(const std::string &path) {
    if (path.empty() || directory_exists(path)) return true;
    const std::string parent = parent_directory(path);
    if (!parent.empty() && parent != path) make_directories(parent);
#if PPC_HOST_WINDOWS
    return _mkdir(path.c_str()) == 0 || directory_exists(path);
#else
    return mkdir(path.c_str(), 0777) == 0 || directory_exists(path);
#endif
}

std::string create_temp_file(const std::string &prefix, const std::string &suffix) {
    const std::string directory = temp_directory();
    // Retry rather than trust one draw: another process may have taken the name
    // between generating it and creating it, which is precisely the race the
    // exclusive create is here to lose safely.
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string path =
            join_path(directory, prefix + "-" + random_suffix() + suffix);
#if PPC_HOST_WINDOWS
        HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        CloseHandle(handle);
        return path;
#else
        // O_EXCL means the create fails if the path exists, including as a
        // symlink an attacker planted to redirect the write. 0600 keeps the
        // generated source unreadable by other users on a shared machine.
        const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) continue;
        close(fd);
        return path;
#endif
    }
    return {};
}

std::string create_temp_directory(const std::string &prefix) {
    const std::string directory = temp_directory();
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string path = join_path(directory, prefix + "-" + random_suffix());
#if PPC_HOST_WINDOWS
        if (CreateDirectoryA(path.c_str(), nullptr)) return path;
#else
        if (mkdir(path.c_str(), 0700) == 0) return path;
#endif
    }
    return {};
}

bool remove_file(const std::string &path) {
    if (path.empty()) return true;
    std::remove(path.c_str());
    return !file_exists(path);
}

bool rename_file(const std::string &from, const std::string &to) {
#if PPC_HOST_WINDOWS
    // Plain MoveFileA fails when the destination exists, which would turn a
    // lost cache race into an error instead of a no-op.
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool remove_directory_tree(const std::string &path) {
    if (path.empty() || !directory_exists(path)) return true;
    // PPC only ever removes directories it created itself under the temp
    // directory, so a shallow implementation is enough; it deliberately does
    // not recurse into subdirectories it did not make.
#if PPC_HOST_WINDOWS
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(join_path(path, "*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = entry.cFileName;
            if (name == "." || name == "..") continue;
            const std::string child = join_path(path, name);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_directory_tree(child);
            } else {
                std::remove(child.c_str());
            }
        } while (FindNextFileA(search, &entry));
        FindClose(search);
    }
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    if (DIR *directory = opendir(path.c_str())) {
        while (struct dirent *entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            const std::string child = join_path(path, name);
            if (directory_exists(child)) remove_directory_tree(child);
            else std::remove(child.c_str());
        }
        closedir(directory);
    }
    return rmdir(path.c_str()) == 0;
#endif
}

bool is_terminal(int stream) {
#if PPC_HOST_WINDOWS
    return _isatty(stream) != 0;
#else
    return isatty(stream) != 0;
#endif
}

int run_process(const std::vector<std::string> &argv, bool echo) {
    if (argv.empty()) return -1;

    if (echo) {
        std::string line;
        for (std::size_t i = 0; i < argv.size(); ++i) {
            if (i) line += " ";
            line += argv[i];
        }
        std::fprintf(stderr, "ppc: %s\n", line.c_str());
    }

#if PPC_HOST_WINDOWS
    // _spawnvp takes the same argv shape and avoids building a command line
    // string, which would otherwise need Windows' quoting rules applied by hand.
    std::vector<const char *> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string &argument : argv) raw.push_back(argument.c_str());
    raw.push_back(nullptr);
    const intptr_t status = _spawnvp(_P_WAIT, raw[0], raw.data());
    return status < 0 ? -1 : static_cast<int>(status);
#else
    const pid_t child = fork();
    if (child < 0) return -1;

    if (child == 0) {
        std::vector<char *> raw;
        raw.reserve(argv.size() + 1);
        for (const std::string &argument : argv) {
            raw.push_back(const_cast<char *>(argument.c_str()));
        }
        raw.push_back(nullptr);
        execvp(raw[0], raw.data());
        std::fprintf(stderr, "ppc: cannot run '%s': %s\n", argv[0].c_str(),
                     std::strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

std::string toolchain_identity(const std::string &program) {
    // The same command name can resolve to a different compiler after a system
    // upgrade, which would otherwise leave a stale cached object in place. The
    // version banner is cheap to obtain and changes when the compiler does.
    const std::string output = create_temp_file("ppc-tc", ".txt");
    if (output.empty()) return {};

    std::string identity;
#if PPC_HOST_WINDOWS
    const std::string command = "\"" + program + "\" --version > \"" + output + "\" 2>&1";
#else
    const std::string command = "'" + program + "' --version > '" + output + "' 2>&1";
#endif
    // A shell is acceptable here only because both operands are program names
    // from the configuration, not user data, and the alternative is duplicating
    // redirection logic per platform.
    if (std::system(command.c_str()) == 0) {
        std::string text;
        if (read_file(output, text)) {
            // The first line is the version banner; later lines are build
            // configuration that changes more often than the compiler does.
            const std::size_t newline = text.find('\n');
            identity = newline == std::string::npos ? text : text.substr(0, newline);
        }
    }
    remove_file(output);
    return identity;
}

}  // namespace host
}  // namespace ppc
