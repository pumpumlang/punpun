#pragma once

#include <cerrno>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ppprocess {

// Launch a process directly, never through a command shell. This matters for a
// compiler because source/output paths are untrusted input and may legitimately
// contain whitespace or shell metacharacters.
inline int run(const std::vector<std::string> &arguments) {
    if (arguments.empty() || arguments.front().empty()) return -1;

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

#ifdef _WIN32
    const intptr_t status = _spawnvp(_P_WAIT, argv.front(), argv.data());
    if (status == -1) return -1;
    return static_cast<int>(status);
#else
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

} // namespace ppprocess
