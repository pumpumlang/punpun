#ifndef PUNPUN_TOOLCHAIN_HPP
#define PUNPUN_TOOLCHAIN_HPP

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace pptoolchain {
namespace fs = std::filesystem;

struct Config {
    std::string profile = "auto";
    std::vector<std::string> cc{"cc"};
    std::vector<std::string> cxx{"c++"};
    std::string linker;
    std::string assembler;
    std::string archiver;
};

inline std::string env_value(const char *name) {
    if (const char *value = std::getenv(name)) return value;
    return {};
}

inline std::vector<fs::path> path_entries() {
    std::vector<fs::path> result;
    const std::string path = env_value("PATH");
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(separator, start);
        const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) result.emplace_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

inline fs::path find_executable(const std::string &name) {
    if (name.empty()) return {};
    fs::path candidate(name);
    std::error_code ec;
    if (candidate.has_parent_path()) {
        if (fs::is_regular_file(candidate, ec)) return fs::absolute(candidate, ec).lexically_normal();
        return {};
    }
    for (const fs::path &root : path_entries()) {
        candidate = root / name;
        if (fs::is_regular_file(candidate, ec)) return fs::absolute(candidate, ec).lexically_normal();
#ifdef _WIN32
        candidate = root / (name + ".exe");
        if (fs::is_regular_file(candidate, ec)) return fs::absolute(candidate, ec).lexically_normal();
#endif
    }
    return {};
}

inline bool available(const std::vector<std::string> &command) {
    return !command.empty() && !find_executable(command.front()).empty();
}

inline Config profile(std::string name) {
    if (name.empty() || name == "auto") {
        Config config;
        config.profile = "auto";
        const std::string configured = env_value("PUNPUN_CC");
        if (!configured.empty()) config.cc = {configured};
        const std::string configured_cxx = env_value("PUNPUN_CXX");
        if (!configured_cxx.empty()) config.cxx = {configured_cxx};
        const std::string configured_linker = env_value("PUNPUN_LINKER");
        if (!configured_linker.empty()) config.linker = configured_linker;
        if (configured.empty()) {
            if (!find_executable("clang").empty()) { config.cc = {"clang"}; config.cxx = {"clang++"}; }
            else if (!find_executable("gcc").empty()) { config.cc = {"gcc"}; config.cxx = {"g++"}; }
            else if (!find_executable("cc").empty()) config.cc = {"cc"};
        }
        return config;
    }
    if (name == "clang") return {"clang", {"clang"}, {"clang++"}, "", "", "llvm-ar"};
    if (name == "gcc") return {"gcc", {"gcc"}, {"g++"}, "", "as", "ar"};
    if (name == "zig") return {"zig", {"zig", "cc"}, {"zig", "c++"}, "", "", ""};
    if (name == "mingw") return {"mingw", {"x86_64-w64-mingw32-gcc"}, {"x86_64-w64-mingw32-g++"}, "", "x86_64-w64-mingw32-as", "x86_64-w64-mingw32-ar"};
    if (name == "msvc") return {"msvc", {"cl.exe"}, {"cl.exe"}, "link.exe", "ml64.exe", "lib.exe"};
    // Custom profiles are represented by their executable name. This keeps the
    // driver extensible without a permanent brand enum; project config can
    // later provide richer adapters around the same structure.
    return {name, {name}, {name}, "", "", ""};
}

inline std::string command_string(const std::vector<std::string> &command) {
    std::ostringstream out;
    for (std::size_t i = 0; i < command.size(); ++i) {
        if (i) out << ' ';
        out << command[i];
    }
    return out.str();
}

inline std::string identity(const Config &config) {
    std::ostringstream out;
    out << "profile=" << config.profile << '\n';
    out << "cc=" << command_string(config.cc) << '\n';
    if (!config.cc.empty()) {
        const fs::path executable = find_executable(config.cc.front());
        out << "cc_path=" << executable.generic_string() << '\n';
        if (!executable.empty()) {
            std::error_code ec;
            const auto size = fs::file_size(executable, ec);
            if (!ec) out << "cc_size=" << size << '\n';
            ec.clear();
            const auto stamp = fs::last_write_time(executable, ec);
            if (!ec) out << "cc_mtime=" << stamp.time_since_epoch().count() << '\n';
        }
    }
    out << "cxx=" << command_string(config.cxx) << '\n';
    out << "linker=" << config.linker << '\n';
    out << "assembler=" << config.assembler << '\n';
    out << "archiver=" << config.archiver << '\n';
    return out.str();
}

inline void append_linker_selection(std::vector<std::string> &command, const Config &config) {
    if (config.linker.empty()) return;
    // GCC/Clang driver syntax. MSVC uses a separate adapter and is rejected by
    // the current portable backend rather than accidentally receiving GNU flags.
    if (config.profile == "msvc") return;
    command.push_back("-fuse-ld=" + config.linker);
}

inline std::vector<std::string> prefix(const std::vector<std::string> &base) {
    return base;
}

} // namespace pptoolchain

#endif
