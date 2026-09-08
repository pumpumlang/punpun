// PunPun compiler driver (`ppc`).
//
// Pipeline:
//   .pp source -> lexer/parser/type checker -> native x86-64 assembly (Linux)
//              -> system assembler/linker -> machine-code executable
//
// A C lowering backend is retained for portability and Windows cross builds.
// It is a compiler backend, not an interpreter: the generated translation unit
// is immediately compiled by the platform C toolchain into native machine code.
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "frontend.hpp"
#include "backend_c.hpp"
#include "backend_x86_64.hpp"
#include "debug_dump.hpp"
#include "semantic.hpp"
#include "formatter.hpp"
#include "process.hpp"
#include "project.hpp"
#include "hir.hpp"
#include "hir_opt.hpp"
#include "mir.hpp"
#include "ownership.hpp"
#include "pipeline.hpp"
#include "toolchain.hpp"

#ifndef PP_RUNTIME_DIR
#define PP_RUNTIME_DIR "runtime"
#endif

#ifndef PP_VERSION
#define PP_VERSION "0.0.0-dev"
#endif

static std::string read_file(const fs::path &path) {
    std::ifstream input(path);
    if (!input) throw Error("error: cannot read '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class Driver {
  public:
    Driver(std::vector<fs::path> include_paths, std::unordered_map<std::string, fs::path> module_paths,
           std::unordered_map<std::string, std::string> overlays = {})
        : include_paths_(std::move(include_paths)), module_paths_(std::move(module_paths)), overlays_(std::move(overlays)) {}

    std::vector<Module> load_program(const fs::path &entry) {
        load(fs::absolute(entry));
        return std::move(modules_);
    }

  private:
    std::vector<fs::path> include_paths_;
    std::unordered_map<std::string, fs::path> module_paths_;
    std::unordered_set<std::string> loaded_;
    std::vector<Module> modules_;
    std::unordered_map<std::string, std::string> overlays_;

    void load(const fs::path &raw_path) {
        std::error_code error;
        const fs::path path = fs::weakly_canonical(raw_path, error);
        const fs::path actual = error ? raw_path.lexically_normal() : path;
        const std::string key = actual.string();
        if (loaded_.count(key)) return;
        if (!fs::is_regular_file(actual)) throw Error("error: source file does not exist: '" + actual.string() + "'");
        loaded_.insert(key);
        std::string source;
        if (auto overlay = overlays_.find(key); overlay != overlays_.end()) source = overlay->second;
        else source = read_file(actual);
        Module module = Parser(Lexer(actual, std::move(source)).scan()).parse(actual);
        const auto imports = module.imports;
        modules_.push_back(std::move(module));
        for (const std::string &name : imports) load(resolve(actual.parent_path(), name));
    }

    fs::path resolve(const fs::path &local, const std::string &name) const {
        if (auto mapped = module_paths_.find(name); mapped != module_paths_.end()) {
            if (!fs::is_regular_file(mapped->second))
                throw Error("error: mapped module '" + name + "' does not exist at '" + mapped->second.string() + "'");
            return mapped->second;
        }
        std::vector<fs::path> roots{local};
        roots.insert(roots.end(), include_paths_.begin(), include_paths_.end());
        for (const auto &root : roots) {
            const fs::path dotted = fs::path(name);
            const fs::path flat = root / (name + ".pp");
            if (fs::is_regular_file(flat)) return flat;
            const fs::path nested = root / dotted / "main.pp";
            if (fs::is_regular_file(nested)) return nested;
            const fs::path source = root / dotted / "src" / "main.pp";
            if (fs::is_regular_file(source)) return source;
        }
        throw Error("error: cannot resolve module '" + name + "'");
    }
};

enum class Target { Native, LinuxX86_64, WindowsX86_64 };

static const char *target_name(Target target) {
    switch (target) {
        case Target::Native: return "native";
        case Target::LinuxX86_64: return "linux-x86_64";
        case Target::WindowsX86_64: return "windows-x86_64";
    }
    return "native";
}

static Target parse_target(const std::string &value) {
    if (value == "native") return Target::Native;
    if (value == "linux-x86_64" || value == "linux" || value == "x86_64-linux") return Target::LinuxX86_64;
    if (value == "windows-x86_64" || value == "windows" || value == "win64" || value == "x86_64-windows")
        return Target::WindowsX86_64;
    throw Error("error: unknown target '" + value + "' (expected native, linux-x86_64, or windows-x86_64)");
}

static bool host_can_use_direct_x86_backend() {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    return true;
#else
    return false;
#endif
}

static void usage() {
    std::cerr
        << "PunPun compiler " PP_VERSION "\n\n"
        << "usage: ppc <build|run|go|check|fmt|emit-tokens|emit-ast|emit-hir|emit-ir|emit-machine-ir|emit-c|emit-asm|emit-llvm> <file.pp> [options] [-- args...]\n\n"
        << "options:\n"
        << "  -o <path>                    Output executable/source path\n"
        << "  -I <path>                    Add an import root (repeatable)\n"
        << "  -M <name=file.pp>            Map an import name to a source file\n"
        << "  --release                    Optimize native output\n"
        << "  --target <target>            native | linux-x86_64 | windows-x86_64\n"
        << "  --windows                    Alias for --target windows-x86_64\n"
        << "  --cc-backend                 Force portable C lowering for native builds\n"
        << "  --llvm-backend               Use Clang/LLVM as the optional native backend\n"
        << "  --toolchain <name>           auto | clang | gcc | zig | mingw | custom executable\n"
        << "  --cc <executable>            Override the C compiler/driver\n"
        << "  --cxx <executable>           Override the C++ compiler for native integrations\n"
        << "  --linker <name>              Select linker through the compiler driver (e.g. lld, mold)\n"
        << "  --check                      With fmt, verify formatting without writing\n"
        << "  --timings                    Print measured compiler phase timings\n"
        << "  --stats                      Print build/cache statistics and timings\n"
        << "  --cache-info                 Explain incremental build cache hits/misses\n"
        << "  --no-cache                   Disable incremental executable reuse\n\n"
        << "LLVM backend uses PUNPUN_LLVM_CC or clang.\n"
        << "Windows cross builds use PUNPUN_WINDOWS_CC or x86_64-w64-mingw32-gcc.\n";
}

static fs::path verified_runtime(fs::path path) {
    const fs::path marker = path / "VERSION";
    if (fs::is_regular_file(marker)) {
        std::ifstream input(marker);
        std::string version;
        std::getline(input, version);
        if (version != PP_VERSION)
            throw Error("error: incompatible PunPun runtime/SDK version '" + version +
                        "' at " + marker.string() + "; compiler requires " PP_VERSION);
    }
    return path;
}

static fs::path runtime_directory() {
    if (const char *configured = std::getenv("PUNPUN_RUNTIME")) return verified_runtime(configured);
#if defined(__linux__)
    std::error_code error;
    const fs::path executable = fs::read_symlink("/proc/self/exe", error);
    if (!error) {
        const fs::path prefix = executable.parent_path().parent_path();
        const fs::path checkout = prefix / "runtime";
        if (fs::is_regular_file(checkout / "punpun.h") && fs::is_regular_file(checkout / "libpunpun.a"))
            return verified_runtime(checkout);
        const fs::path installed = prefix / "lib/punpun";
        if (fs::is_regular_file(installed / "punpun.h")) return verified_runtime(installed);
    }
#elif defined(_WIN32)
    std::vector<char> buffer(32768, '\0');
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        const fs::path executable(std::string(buffer.data(), length));
        const fs::path prefix = executable.parent_path().parent_path();
        const fs::path sdk_runtime = prefix / "runtime";
        if (fs::is_regular_file(sdk_runtime / "punpun.h") && fs::is_regular_file(sdk_runtime / "punpun.c"))
            return verified_runtime(sdk_runtime);
        const fs::path installed = prefix / "lib/punpun";
        if (fs::is_regular_file(installed / "punpun.h")) return verified_runtime(installed);
    }
#endif
    return verified_runtime(PP_RUNTIME_DIR);
}

struct TemporarySource {
    fs::path path;
    explicit TemporarySource(const char *suffix) {
        const fs::path root = fs::temp_directory_path();
        std::random_device device;
        std::mt19937_64 random(device());
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = root / ("punpun-" + std::to_string(random()) + suffix);
            if (fs::exists(path)) continue;
            std::ofstream create(path, std::ios::binary);
            if (create) return;
        }
        throw Error("error: cannot create temporary file");
    }
    ~TemporarySource() {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
};

struct TemporaryOutput {
    fs::path final_path;
    fs::path staging_path;
    bool committed = false;

    explicit TemporaryOutput(fs::path output) : final_path(std::move(output)) {
        if (final_path.has_parent_path()) fs::create_directories(final_path.parent_path());
        const fs::path directory = final_path.has_parent_path() ? final_path.parent_path() : fs::path(".");
        std::random_device device;
        std::mt19937_64 random(device());
        for (int attempt = 0; attempt < 100; ++attempt) {
            staging_path = directory / (final_path.filename().string() + ".tmp-" + std::to_string(random()));
            std::error_code ec;
            if (!fs::exists(staging_path, ec)) return;
        }
        throw Error("error: cannot allocate staging output beside '" + final_path.string() + "'");
    }

    void commit() {
        std::error_code ec;
#if defined(_WIN32)
        // MoveFileEx provides replace-in-place semantics without deleting the
        // previous known-good executable first. A failed replacement therefore
        // cannot leave the user with a missing artifact.
        if (!MoveFileExA(staging_path.string().c_str(), final_path.string().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw Error("error: cannot install build output '" + final_path.string() +
                        "' (Windows error " + std::to_string(GetLastError()) + ")");
        }
#else
        fs::rename(staging_path, final_path, ec);
        if (ec) throw Error("error: cannot atomically install build output '" + final_path.string() + "': " + ec.message());
#endif
        committed = true;
    }

    ~TemporaryOutput() {
        if (committed) return;
        std::error_code ignored;
        fs::remove(staging_path, ignored);
    }
};


static std::string env_or(const char *name, const char *fallback) {
    if (const char *value = std::getenv(name); value && *value) return value;
    return fallback;
}

static void append_common_warnings(std::vector<std::string> &arguments) {
    arguments.insert(arguments.end(), {
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-Wno-unused-variable", "-Wno-unused-but-set-variable"
    });
}

static std::string json_escape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0f];
                    out += hex[c & 0x0f];
                } else out += static_cast<char>(c);
        }
    }
    return out;
}

static void print_language_info() {
    std::cout << "{\n  \"compiler_version\": \"" PP_VERSION "\",\n";
    std::cout << "  \"types\": [\"i64\",\"i32\",\"u64\",\"u32\",\"f64\",\"f32\",\"bool\",\"String\",\"nums\",\"void\"],\n";
    std::cout << "  \"keywords\": [\"bring\",\"launch\",\"say\",\"fn\",\"struct\",\"object\",\"contract\",\"meets\",\"sealed\",\"init\",\"let\",\"mut\",\"const\",\"return\",\"if\",\"else\",\"while\",\"for\",\"in\",\"break\",\"continue\",\"import\",\"true\",\"false\",\"unsafe\",\"raw\",\"public\",\"private\",\"protected\",\"extern\",\"native\",\"self\",\"async\",\"await\",\"enum\",\"match\",\"case\",\"where\"],\n";
    std::cout << "  \"legacy_keywords\": [\"craft\",\"gives\",\"as\",\"shape\",\"done\",\"pin\",\"keep\",\"give\",\"when\",\"otherwise\",\"whilst\",\"each\",\"from\",\"until\",\"leave\",\"next\",\"yes\",\"no\",\"and\",\"or\",\"not\"],\n";
    std::cout << "  \"builtins\": [\n";
    const auto &builtins = punpun_builtins();
    for (std::size_t i = 0; i < builtins.size(); ++i) {
        const BuiltinSpec &builtin = builtins[i];
        std::cout << "    {\"name\":\"" << json_escape(builtin.name) << "\",\"parameters\":[";
        for (std::size_t p = 0; p < builtin.parameters.size(); ++p) {
            if (p) std::cout << ",";
            std::cout << "\"" << json_escape(type_name(builtin.parameters[p])) << "\"";
        }
        std::cout << "],\"result\":\"" << json_escape(type_name(builtin.result))
                  << "\",\"documentation\":\"" << json_escape(builtin.documentation) << "\"}";
        if (i + 1 != builtins.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    const fs::path runtime = runtime_directory();
    const fs::path root = fs::is_directory(runtime / "stdlib") ? runtime / "stdlib" : runtime.parent_path() / "stdlib";
    struct StdSymbol { std::string module; const ::Function *function; };
    std::vector<std::string> modules;
    std::vector<Module> parsed_stdlib;
    std::error_code error;
    if (fs::is_directory(root, error)) {
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
            if (it->is_regular_file() && it->path().extension() == ".pp") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files) {
            fs::path relative = fs::relative(file, root, error);
            if (error) break;
            relative.replace_extension();
            std::string name = relative.generic_string();
            for (char &c : name) if (c == '/') c = '.';
            modules.push_back(name);
            parsed_stdlib.push_back(Parser(Lexer(file, read_file(file)).scan()).parse(file));
        }
    }
    std::cout << "  \"modules\": [";
    for (std::size_t i = 0; i < modules.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << "\"" << json_escape(modules[i]) << "\"";
    }
    std::cout << "],\n  \"stdlib_symbols\": [\n";
    bool first_symbol = true;
    for (std::size_t m = 0; m < parsed_stdlib.size(); ++m) {
        for (const ::Function &function : parsed_stdlib[m].functions) {
            if (!first_symbol) std::cout << ",\n";
            first_symbol = false;
            std::cout << "    {\"module\":\"" << json_escape(modules[m])
                      << "\",\"name\":\"" << json_escape(function.name) << "\",\"parameters\":[";
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << json_escape(function.parameters[i].name)
                          << "\",\"type\":\"" << json_escape(type_name(function.parameters[i].type)) << "\"}";
            }
            std::cout << "],\"result\":\"" << json_escape(type_name(function.result)) << "\"}";
        }
    }
    std::cout << "\n  ]\n}\n";
}


static uint64_t fnv1a_append(uint64_t hash, const char *data, std::size_t size) {
    constexpr uint64_t prime = UINT64_C(1099511628211);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= prime;
    }
    return hash;
}

static uint64_t fnv1a_append(uint64_t hash, const std::string &value) {
    return fnv1a_append(hash, value.data(), value.size());
}

static std::string hex_hash(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

static std::string build_fingerprint(const std::vector<Module> &modules, const fs::path &runtime,
                                     Target target, bool release, bool cc_backend, bool llvm_backend,
                                     const pptoolchain::Config &toolchain) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&](const std::string &value) { hash = fnv1a_append(hash, value); };
    add("PunPun-" PP_VERSION "\n");
    add(target_name(target));
    add(release ? "\nrelease\n" : "\ndebug\n");
    add(llvm_backend ? "llvm-backend\n" : (cc_backend ? "cc-backend\n" : "native-backend\n"));
    add(pptoolchain::identity(toolchain));
    std::vector<fs::path> sources;
    for (const Module &module : modules) sources.push_back(fs::absolute(module.file).lexically_normal());
    std::sort(sources.begin(), sources.end());
    for (const fs::path &source : sources) {
        add(source.string()); add("\0"); add(read_file(source)); add("\0");
    }
    const fs::path runtime_file = runtime / (target == Target::WindowsX86_64 ? "punpun.c" : "libpunpun.a");
    if (fs::is_regular_file(runtime_file)) { add(runtime_file.string()); add(read_file(runtime_file)); }
    return hex_hash(hash);
}

static std::string c_line_path(const fs::path &path) {
    std::string value = fs::absolute(path).lexically_normal().string();
    std::string out;
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

static std::vector<fs::path> compile_injections(const std::vector<Module> &modules, Target target, bool release,
                                                const pptoolchain::Config &toolchain) {
    std::vector<fs::path> objects;
    bool any = false;
    for (const Module &module : modules) if (!module.injections.empty()) { any = true; break; }
    if (!any) return objects; // zero compiler discovery/process tax for ordinary PunPun builds.

    const bool windows_target = target == Target::WindowsX86_64;
    std::vector<std::string> cc = toolchain.cc;
    std::vector<std::string> cxx = toolchain.cxx;
    if (windows_target) {
        const std::string configured = env_or("PUNPUN_WINDOWS_CC", "");
        if (!configured.empty()) cc = {configured};
        else if (toolchain.profile == "auto") cc = {"x86_64-w64-mingw32-gcc"};
        const std::string configured_cxx = env_or("PUNPUN_WINDOWS_CXX", "");
        if (!configured_cxx.empty()) cxx = {configured_cxx};
        else if (toolchain.profile == "auto") cxx = {"x86_64-w64-mingw32-g++"};
    } else {
        const std::string configured = env_or("PUNPUN_INJECT_CC", "");
        if (!configured.empty()) cc = {configured};
        const std::string configured_cxx = env_or("PUNPUN_INJECT_CXX", "");
        if (!configured_cxx.empty()) cxx = {configured_cxx};
    }
    const fs::path cache = fs::path(".punpun/cache/inject");
    fs::create_directories(cache);

    for (const Module &module : modules) for (const ForeignInjection &injection : module.injections) {
        const std::string language = injection.language;
        const bool is_cpp = language == "cpp" || language == "cxx";
        const bool is_rust = language == "rust";
        const bool is_asm = language == "asm";
        const std::vector<std::string> compiler = is_cpp ? cxx : (is_rust
            ? std::vector<std::string>{env_or("PUNPUN_INJECT_RUSTC", "rustc")} : cc);
        uint64_t hash = UINT64_C(14695981039346656037);
        hash = fnv1a_append(hash, "PunPun-inject-" + language + "-v2\n");
        hash = fnv1a_append(hash, injection.source);
        hash = fnv1a_append(hash, "\ncompiler="); hash = fnv1a_append(hash, pptoolchain::command_string(compiler));
        hash = fnv1a_append(hash, "\ntoolchain="); hash = fnv1a_append(hash, pptoolchain::identity(toolchain));
        hash = fnv1a_append(hash, "\ntarget="); hash = fnv1a_append(hash, target_name(target));
        hash = fnv1a_append(hash, release ? "\nrelease" : "\ndebug");
        const std::string key = hex_hash(hash);
        const std::string source_extension = is_cpp ? ".cpp" : (is_rust ? ".rs" : (is_asm ? ".S" : ".c"));
        const std::string object_extension = is_rust ? ".a" : (windows_target ? ".obj" : ".o");
        const fs::path source_path = cache / (key + source_extension);
        const fs::path object_path = cache / (key + object_extension);
        if (!fs::is_regular_file(object_path)) {
            std::ofstream foreign(source_path, std::ios::binary | std::ios::trunc);
            if (!foreign) throw Error("error[E0603]: cannot create cached " + language + " injection source");
            if (!is_rust)
                foreign << "#line " << injection.source_token.line << " \"" << c_line_path(module.file) << "\"\n";
            else
                foreign << "// PunPun source: " << c_line_path(module.file) << ":" << injection.source_token.line << "\n";
            foreign << injection.source;
            if (!injection.source.empty() && injection.source.back() != '\n') foreign << '\n';
            foreign.close();
            std::vector<std::string> command = compiler;
            if (is_rust) {
                command.insert(command.end(), {"--crate-type", "staticlib", "--edition", "2021",
                                               "-C", release ? "opt-level=2" : "opt-level=0"});
                if (windows_target) command.insert(command.end(), {"--target", "x86_64-pc-windows-gnu"});
                command.insert(command.end(), {source_path.string(), "-o", object_path.string()});
            } else {
                if (!is_asm) command.push_back(is_cpp ? "-std=c++20" : "-std=c17");
                command.insert(command.end(), {release ? "-O2" : "-O0", "-I" + runtime_directory().string(),
                                               "-c", source_path.string(), "-o", object_path.string()});
            }
            const int status = ppprocess::run(command);
            if (status != 0) {
                std::error_code ignored; fs::remove(object_path, ignored);
                throw Error(ppdiag::format(module.file, injection.token.line, injection.token.column,
                                           injection.token.length, "E0603", language + " injection failed",
                                           language + " injection failed", "install/configure its compiler and fix the foreign diagnostic shown above"));
            }
        }
        objects.push_back(object_path);
    }
    return objects;
}

static bool has_cpp_injection(const std::vector<Module> &modules) {
    for (const Module &module : modules) for (const ForeignInjection &injection : module.injections)
        if (injection.language == "cpp" || injection.language == "cxx") return true;
    return false;
}

static fs::path cache_record_path(const fs::path &input, const fs::path &output,
                                  Target target, bool release, bool cc_backend, bool llvm_backend) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const std::string identity = fs::absolute(input).lexically_normal().string() + "\n" +
                                 fs::absolute(output).lexically_normal().string() + "\n" +
                                 target_name(target) + (release ? "\nrelease" : "\ndebug") +
                                 (llvm_backend ? "\nllvm" : (cc_backend ? "\ncc" : "\ndirect"));
    hash = fnv1a_append(hash, identity);
    return fs::path(".punpun/cache") / ("build-" + hex_hash(hash) + ".fingerprint");
}

static bool read_cache_fingerprint(const fs::path &path, std::string &value) {
    std::ifstream input(path);
    if (!input) return false;
    std::getline(input, value);
    return !value.empty();
}

static void write_cache_fingerprint(const fs::path &path, const std::string &value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error("error: cannot write incremental cache metadata '" + path.string() + "'");
    output << value << "\n";
}


static int semantic_worker() {
    const fs::path runtime = runtime_directory();
    const fs::path stdlib = fs::is_directory(runtime / "stdlib") ? runtime / "stdlib" : runtime.parent_path() / "stdlib";
    std::string header;
    while (std::getline(std::cin, header)) {
        if (header == "QUIT") return 0;
        std::istringstream parts(header);
        std::string command;
        std::size_t path_size = 0, source_size = 0;
        parts >> command >> path_size >> source_size;
        if (command != "CHECK" || !parts || path_size > (1u << 20) || source_size > (64u << 20)) {
            const std::string message = "error[E9000]: malformed semantic-worker request";
            std::cout << "RESULT " << message.size() << "\n" << message << std::flush;
            continue;
        }
        std::string path_text(path_size, '\0');
        std::string source(source_size, '\0');
        std::cin.read(path_text.data(), static_cast<std::streamsize>(path_size));
        std::cin.read(source.data(), static_cast<std::streamsize>(source_size));
        std::string result;
        try {
            const fs::path path = fs::absolute(fs::path(path_text)).lexically_normal();
            std::unordered_map<std::string, std::string> overlays{{path.string(), std::move(source)}};
            std::vector<fs::path> include_paths{stdlib};
            std::unordered_map<std::string, fs::path> mappings;
            const fs::path project_root = ppproject::find_root(path);
            if (!project_root.empty()) {
                ppproject::Graph graph(project_root);
                for (const fs::path &root : graph.include_paths()) include_paths.push_back(root);
                mappings = graph.module_mappings();
            }
            std::vector<Module> modules = Driver(std::move(include_paths), std::move(mappings), std::move(overlays)).load_program(path);
            SemanticAnalyzer(modules).analyze();
            ppownership::Analyzer(modules).analyze();
        } catch (const Error &error) { result = error.what(); }
          catch (const std::exception &error) { result = std::string("error[E9001]: internal semantic worker failure: ") + error.what(); }
        std::cout << "RESULT " << result.size() << "\n";
        if (!result.empty()) std::cout.write(result.data(), static_cast<std::streamsize>(result.size()));
        std::cout << std::flush;
    }
    return 0;
}

int main(int argc, char **argv) {
    const auto process_started = std::chrono::steady_clock::now();
    try {
        if (argc == 2 && std::string(argv[1]) == "semantic-worker") return semantic_worker();
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "ppc " PP_VERSION "\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "language-info") {
            print_language_info();
            return 0;
        }
        if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "help")) {
            usage();
            return 0;
        }
        if (argc >= 2) {
            const std::string package_command = argv[1];
            if (package_command == "package-add") {
                if (argc != 4) throw Error("error: usage: pp add <name> <path>");
                const fs::path root = ppproject::find_root(fs::current_path());
                if (root.empty()) throw Error("error: pp add requires a Punpun.toml project");
                ppproject::edit_dependency(root, argv[2], argv[3], false);
                ppproject::Graph graph(root); graph.write_lockfile();
                std::cout << "added " << argv[2] << " -> " << argv[3] << "\n";
                return 0;
            }
            if (package_command == "package-remove") {
                if (argc != 3) throw Error("error: usage: pp remove <name>");
                const fs::path root = ppproject::find_root(fs::current_path());
                if (root.empty()) throw Error("error: pp remove requires a Punpun.toml project");
                ppproject::edit_dependency(root, argv[2], {}, true);
                ppproject::Graph graph(root); graph.write_lockfile();
                std::cout << "removed " << argv[2] << "\n";
                return 0;
            }
            if (package_command == "package-tree" || package_command == "package-update" || package_command == "package-fetch") {
                if (argc != 2) throw Error("error: package command accepts no arguments");
                const fs::path root = ppproject::find_root(fs::current_path());
                if (root.empty()) throw Error("error: package command requires a Punpun.toml project");
                ppproject::Graph graph(root);
                graph.write_lockfile();
                if (package_command == "package-tree") std::cout << graph.tree();
                else std::cout << (package_command == "package-fetch" ? "dependencies available in local cache/path graph\n" : "lockfile updated\n");
                return 0;
            }
        }
        if (argc < 3) {
            usage();
            return 2;
        }

        const std::string command = std::string(argv[1]) == "go" ? "run" : argv[1];
        if (command != "build" && command != "run" && command != "check" && command != "fmt" &&
            command != "emit-tokens" && command != "emit-ast" && command != "emit-hir" && command != "emit-ir" &&
            command != "emit-machine-ir" &&
            command != "emit-c" && command != "emit-asm" && command != "emit-llvm") {
            usage();
            return 2;
        }

        fs::path input = argv[2];
        if (input.extension() != ".pp") throw Error("error: PunPun source files must use the .pp extension");

        Target target = Target::Native;
        fs::path output;
        bool explicit_output = false;
        bool release = false;
        bool cc_backend = false;
        bool llvm_backend = false;
        bool format_check = false;
        bool show_timings = false;
        bool show_stats = false;
        bool cache_info = false;
        bool use_cache = true;
        bool offline = false;
        std::vector<fs::path> include_paths;
        const fs::path runtime = runtime_directory();
        const fs::path stdlib = fs::is_directory(runtime / "stdlib") ? runtime / "stdlib" : runtime.parent_path() / "stdlib";
        pptoolchain::Config toolchain = pptoolchain::profile("auto");
        bool toolchain_cli_override = false;
        include_paths.push_back(stdlib);
        std::unordered_map<std::string, fs::path> module_paths;
        std::vector<std::string> program_arguments;
        bool after_separator = false;

        for (int i = 3; i < argc; ++i) {
            const std::string argument = argv[i];
            if (after_separator) program_arguments.push_back(argument);
            else if (argument == "--") after_separator = true;
            else if (argument == "-o" && i + 1 < argc) { output = argv[++i]; explicit_output = true; }
            else if (argument == "--release") release = true;
            else if (argument == "--cc-backend") cc_backend = true;
            else if (argument == "--llvm-backend") llvm_backend = true;
            else if (argument == "--toolchain" && i + 1 < argc) { toolchain = pptoolchain::profile(argv[++i]); toolchain_cli_override = true; }
            else if (argument == "--cc" && i + 1 < argc) { toolchain.cc = {argv[++i]}; toolchain_cli_override = true; }
            else if (argument == "--cxx" && i + 1 < argc) { toolchain.cxx = {argv[++i]}; toolchain_cli_override = true; }
            else if (argument == "--linker" && i + 1 < argc) { toolchain.linker = argv[++i]; toolchain_cli_override = true; }
            else if (argument == "--timings") show_timings = true;
            else if (argument == "--stats") { show_stats = true; show_timings = true; }
            else if (argument == "--cache-info" || argument == "--explain") cache_info = true;
            else if (argument == "--no-cache") use_cache = false;
            else if (argument == "--offline") offline = true;
            else if (argument == "--check" && command == "fmt") format_check = true;
            else if (argument == "--windows") target = Target::WindowsX86_64;
            else if (argument == "--target" && i + 1 < argc) target = parse_target(argv[++i]);
            else if (argument == "-I" && i + 1 < argc) include_paths.emplace_back(argv[++i]);
            else if (argument == "-M" && i + 1 < argc) {
                const std::string mapping = argv[++i];
                const size_t separator = mapping.find('=');
                if (separator == std::string::npos || separator == 0 || separator + 1 == mapping.size())
                    throw Error("error: module mappings use -M name=file");
                module_paths[mapping.substr(0, separator)] = mapping.substr(separator + 1);
            } else {
                throw Error("error: unknown option '" + argument + "'");
            }
        }

        if (after_separator && command != "run") throw Error("error: program arguments require run or go");
        if (cc_backend && llvm_backend) throw Error("error: --cc-backend and --llvm-backend are mutually exclusive");
        if (llvm_backend && target == Target::WindowsX86_64)
            throw Error("error: the LLVM compatibility backend is currently native-host only");
        const std::string llvm_cc = env_or("PUNPUN_LLVM_CC", "clang");
        if ((llvm_backend || command == "emit-llvm") && !pptoolchain::available({llvm_cc}))
            throw Error("error: LLVM backend requires Clang; install clang or set PUNPUN_LLVM_CC");
        // Package infrastructure is lazy. Single-file builds never instantiate
        // a resolver. Projects with Punpun.toml load only their local/path graph.
        const fs::path project_root = ppproject::find_root(input);
        if (!project_root.empty()) {
            ppproject::Graph graph(project_root);
            graph.write_lockfile();
            const ppproject::Manifest &manifest = graph.root_manifest();
            if (!toolchain_cli_override) {
                if (!manifest.toolchain_profile.empty()) toolchain = pptoolchain::profile(manifest.toolchain_profile);
                if (!manifest.toolchain_cc.empty()) toolchain.cc = {manifest.toolchain_cc};
                if (!manifest.toolchain_cxx.empty()) toolchain.cxx = {manifest.toolchain_cxx};
                if (!manifest.toolchain_linker.empty()) toolchain.linker = manifest.toolchain_linker;
            }
            for (const fs::path &path : graph.include_paths()) include_paths.push_back(path);
            for (const auto &[name, path] : graph.module_mappings())
                if (!module_paths.count(name)) module_paths[name] = path;
        }
        if (toolchain.profile == "msvc")
            throw Error("error: the MSVC adapter is detected by tooling but direct MSVC code generation is not implemented in this beta; use clang/gcc/MinGW for builds");
        if (!pptoolchain::available(toolchain.cc))
            throw Error("error: selected C/compiler driver is unavailable: '" + pptoolchain::command_string(toolchain.cc) + "'");
        (void)offline; // all currently-supported dependencies are local/path and therefore offline-safe.
        if (command == "run" && target == Target::WindowsX86_64 &&
#if defined(_WIN32)
            false
#else
            true
#endif
        ) {
            throw Error("error: cannot run a Windows .exe on this host; use 'build' instead");
        }

        if (!explicit_output) {
            output = fs::path(".punpun/bin") / input.stem();
            if (target == Target::WindowsX86_64) output += ".exe";
#ifdef _WIN32
            else if (target == Target::Native) output += ".exe";
#endif
        }

        if (command == "fmt") {
            if (explicit_output) throw Error("error: fmt writes the source in place and does not accept -o");
            const std::string original = read_file(input);
            const std::string formatted = ppfmt::format_source(original);
            if (format_check) {
                if (formatted != original) {
                    std::cerr << "format check failed: " << input.string() << " needs formatting\n";
                    return 1;
                }
                std::cout << "formatted " << input.string() << " [unchanged]\n";
                return 0;
            }
            if (formatted != original) {
                std::ofstream stream(input, std::ios::binary | std::ios::trunc);
                if (!stream) throw Error("error: cannot write '" + input.string() + "'");
                stream << formatted;
                if (!stream) throw Error("error: failed to write formatted source");
                std::cout << "formatted " << input.string() << "\n";
            } else {
                std::cout << "formatted " << input.string() << " [unchanged]\n";
            }
            return 0;
        }

        if (explicit_output && (fs::weakly_canonical(input) == fs::weakly_canonical(output) || output.extension() == ".pp"))
            throw Error("error: output must not overwrite PunPun source");

        if (command == "emit-tokens") {
            const auto tokens = Lexer(fs::absolute(input), read_file(fs::absolute(input))).scan();
            const std::string dumped = ppdebug::dump_tokens(tokens);
            if (!explicit_output) std::cout << dumped;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output);
                if (!stream) throw Error("error: cannot write '" + output.string() + "'");
                stream << dumped;
            }
            return 0;
        }

        const auto load_started = std::chrono::steady_clock::now();
        std::vector<Module> modules = Driver(include_paths, module_paths).load_program(input);
        const auto load_finished = std::chrono::steady_clock::now();

        if (command == "emit-ast") {
            const std::string dumped = ppdebug::dump_modules(modules);
            if (!explicit_output) std::cout << dumped;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output);
                if (!stream) throw Error("error: cannot write '" + output.string() + "'");
                stream << dumped;
            }
            return 0;
        }

        const auto semantic_started = std::chrono::steady_clock::now();
        SemanticAnalyzer(modules).analyze();
        ppownership::Analyzer(modules).analyze();
        const auto semantic_finished = std::chrono::steady_clock::now();
        const auto ir_started = std::chrono::steady_clock::now();
        pppipeline::Result pipeline = pppipeline::build(modules, release);
        const auto ir_finished = std::chrono::steady_clock::now();

        const auto milliseconds = [](auto start, auto end) {
            return std::chrono::duration<double, std::milli>(end - start).count();
        };
        const auto print_frontend_timings = [&]() {
            if (!show_timings) return;
            std::cerr << std::fixed << std::setprecision(3)
                      << "timing load+parse  " << milliseconds(load_started, load_finished) << " ms\n"
                      << "timing semantic    " << milliseconds(semantic_started, semantic_finished) << " ms\n"
                      << "timing HIR+MIR+MIR2 " << milliseconds(ir_started, ir_finished) << " ms\n";
        };

        if (command == "check") {
            std::cout << "checked " << input.string() << "\n";
            print_frontend_timings();
            if (show_stats) {
                std::size_t functions = 0;
                for (const Module &module : modules) functions += module.functions.size();
                std::cerr << "stats files        " << modules.size() << "\n"
                          << "stats functions    " << functions << "\n"
                          << "stats codegen      skipped (check)\n";
            }
            if (show_timings) std::cerr << "timing total       " << milliseconds(process_started, std::chrono::steady_clock::now()) << " ms\n";
            return 0;
        }

        if (command == "emit-hir" || command == "emit-ir" || command == "emit-machine-ir") {
            const std::string dumped = command == "emit-machine-ir"
                ? ppmachine::dump(pipeline.machine)
                : (command == "emit-ir" ? ppmir::dump(pipeline.mir) : pphir::dump(pipeline.hir));
            if (!explicit_output) std::cout << dumped;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output);
                if (!stream) throw Error("error: cannot write '" + output.string() + "'");
                stream << dumped;
            }
            return 0;
        }

        if (command == "emit-c") {
            const std::string generated = CBackend(modules, pipeline.machine).generate();
            if (!explicit_output) std::cout << generated;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output);
                if (!stream) throw Error("error: cannot write '" + output.string() + "'");
                stream << generated;
                if (!stream) throw Error("error: failed to write generated C");
            }
            return 0;
        }

        if (command == "emit-asm") {
            if (target == Target::WindowsX86_64)
                throw Error("error: emit-asm currently supports the Linux x86-64 backend only");
            const std::string generated = X86Backend(modules, pipeline.machine).generate();
            if (!explicit_output) std::cout << generated;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output);
                if (!stream) throw Error("error: cannot write '" + output.string() + "'");
                stream << generated;
                if (!stream) throw Error("error: failed to write generated assembly");
            }
            return 0;
        }

        if (command == "emit-llvm") {
            if (target == Target::WindowsX86_64) throw Error("error: emit-llvm is currently native-host only");
            const std::string generated = CBackend(modules, pipeline.machine).generate();
            TemporarySource c_source(".c");
            { std::ofstream stream(c_source.path); if (!stream) throw Error("error: cannot create temporary C file"); stream << generated; }
            TemporarySource llvm_output(".ll");
            std::vector<std::string> llvm{llvm_cc, "-std=c17", "-S", "-emit-llvm", release ? "-O3" : "-O0",
                                          "-I" + runtime.string(), c_source.path.string(), "-o", llvm_output.path.string()};
            append_common_warnings(llvm);
            if (ppprocess::run(llvm) != 0) throw Error("error: LLVM IR emission failed");
            const std::string ir = read_file(llvm_output.path);
            if (!explicit_output) std::cout << ir;
            else {
                if (output.has_parent_path()) fs::create_directories(output.parent_path());
                std::ofstream stream(output); if (!stream) throw Error("error: cannot write LLVM IR output"); stream << ir;
            }
            return 0;
        }

        if (output.has_parent_path()) fs::create_directories(output.parent_path());

        const fs::path cache_path = cache_record_path(input, output, target, release, cc_backend, llvm_backend);
        std::string fingerprint = build_fingerprint(modules, runtime, target, release, cc_backend, llvm_backend, toolchain);
        fingerprint += "-" + pipeline.program_hash + (llvm_backend ? ("-llvm-" + llvm_cc) : "");
        bool cache_hit = false;
        std::string cached_fingerprint;
        if (use_cache && fs::is_regular_file(output) && read_cache_fingerprint(cache_path, cached_fingerprint) &&
            cached_fingerprint == fingerprint) cache_hit = true;
        if (cache_info) {
            if (cache_hit) std::cerr << "CACHE HIT  " << input.string() << " -> " << output.string() << "\n";
            else if (!use_cache) std::cerr << "CACHE MISS " << input.string() << " (cache disabled)\n";
            else if (!fs::is_regular_file(output)) std::cerr << "CACHE MISS " << input.string() << " (output missing)\n";
            else if (cached_fingerprint.empty()) std::cerr << "CACHE MISS " << input.string() << " (no prior fingerprint)\n";
            else std::cerr << "CACHE MISS " << input.string() << " (source/dependency/configuration changed)\n";
        }
        if (cache_hit) {
            print_frontend_timings();
            if (show_stats) {
                std::size_t functions = 0;
                for (const Module &module : modules) functions += module.functions.size();
                std::cerr << "stats files        " << modules.size() << "\n"
                          << "stats modules      " << modules.size() << " reused, 0 rebuilt\n"
                          << "stats functions    " << functions << " reused, 0 rebuilt\n"
                          << "stats cache        HIT\n";
            }
            if (command == "run") {
                const auto execution_started = std::chrono::steady_clock::now();
                std::vector<std::string> run{fs::absolute(output).string()};
                run.insert(run.end(), program_arguments.begin(), program_arguments.end());
                const int run_status = ppprocess::run(run);
                if (run_status == -1) throw Error("error: could not start executable");
                if (show_timings) {
                    const auto end = std::chrono::steady_clock::now();
                    std::cerr << "timing execute     " << milliseconds(execution_started, end) << " ms\n"
                              << "timing total       " << milliseconds(process_started, end) << " ms\n";
                }
                return run_status;
            }
            std::cout << "built " << output.string() << " [" << target_name(target) << "] [cached]\n";
            if (show_timings) std::cerr << "timing total       " << milliseconds(process_started, std::chrono::steady_clock::now()) << " ms\n";
            return 0;
        }

        const std::vector<fs::path> injection_objects = compile_injections(modules, target, release, toolchain);

        const auto codegen_started = std::chrono::steady_clock::now();
        TemporaryOutput staged_output(output);
        std::unique_ptr<TemporarySource> temporary;
        std::vector<std::string> build;
        const bool windows_target = target == Target::WindowsX86_64;
        const bool direct_native = !windows_target && !cc_backend && !llvm_backend && host_can_use_direct_x86_backend();

        if (direct_native) {
            const std::string generated = X86Backend(modules, pipeline.machine).generate();
            temporary = std::make_unique<TemporarySource>(".s");
            std::ofstream stream(temporary->path);
            if (!stream) throw Error("error: cannot create temporary assembly file");
            stream << generated;
            if (!stream) throw Error("error: failed to write temporary assembly file");

            build = toolchain.cc;
            build.push_back(release ? "-O2" : "-g");
            pptoolchain::append_linker_selection(build, toolchain);
            build.push_back(temporary->path.string());
            for (const fs::path &object : injection_objects) build.push_back(object.string());
            build.push_back((runtime / "libpunpun.a").string());
            if (has_cpp_injection(modules)) build.push_back("-lstdc++");
            build.push_back("-lm");
            build.push_back("-pthread");
            build.push_back("-o");
            build.push_back(staged_output.staging_path.string());
        } else {
            const std::string generated = CBackend(modules, pipeline.machine).generate();
            temporary = std::make_unique<TemporarySource>(".c");
            std::ofstream stream(temporary->path);
            if (!stream) throw Error("error: cannot create temporary C file");
            stream << generated;
            if (!stream) throw Error("error: failed to write temporary C file");

            build = llvm_backend ? std::vector<std::string>{llvm_cc} : toolchain.cc;
            if (windows_target && toolchain.profile == "auto") {
                const std::string configured = env_or("PUNPUN_WINDOWS_CC", "x86_64-w64-mingw32-gcc");
                build = {configured};
            }
            build.push_back("-std=c17");
            build.push_back(release ? "-O3" : "-O0");
            pptoolchain::append_linker_selection(build, toolchain);
            if (release) build.push_back("-flto");
            else build.push_back("-g");
            append_common_warnings(build);
            build.push_back("-I" + runtime.string());
            build.push_back(temporary->path.string());
            for (const fs::path &object : injection_objects) build.push_back(object.string());
            if (windows_target) {
                if (!fs::is_regular_file(runtime / "punpun.c"))
                    throw Error("error: Windows builds require runtime/punpun.c beside the runtime library");
                build.push_back((runtime / "punpun.c").string());
                build.push_back("-static");
                if (has_cpp_injection(modules)) build.push_back("-lstdc++");
                build.push_back("-lm");
            } else {
                build.push_back((runtime / "libpunpun.a").string());
                if (has_cpp_injection(modules)) build.push_back("-lstdc++");
                build.push_back("-lm");
                build.push_back("-pthread");
            }
            build.push_back("-o");
            build.push_back(staged_output.staging_path.string());
        }

        const auto codegen_finished = std::chrono::steady_clock::now();
        const auto link_started = codegen_finished;
        const int status = ppprocess::run(build);
        const auto link_finished = std::chrono::steady_clock::now();
        if (status != 0) {
            if (windows_target)
                throw Error("error: Windows native compilation failed. Install MinGW-w64 or set PUNPUN_WINDOWS_CC to a compatible x86-64 Windows C compiler");
            throw Error("error: native compilation failed");
        }
        staged_output.commit();
        if (use_cache) write_cache_fingerprint(cache_path, fingerprint);
        print_frontend_timings();
        if (show_timings) {
            std::cerr << "timing codegen     " << milliseconds(codegen_started, codegen_finished) << " ms\n"
                      << "timing assemble+link " << milliseconds(link_started, link_finished) << " ms\n";
        }
        if (show_stats) {
            std::size_t functions = 0;
            for (const Module &module : modules) functions += module.functions.size();
            std::cerr << "stats files        " << modules.size() << "\n"
                      << "stats modules      0 reused, " << modules.size() << " rebuilt\n"
                      << "stats functions    0 reused, " << functions << " rebuilt\n"
                      << "stats cache        " << (use_cache ? "MISS" : "DISABLED") << "\n";
        }

        if (command == "run") {
            std::vector<std::string> run{fs::absolute(output).string()};
            run.insert(run.end(), program_arguments.begin(), program_arguments.end());
            const auto execution_started = std::chrono::steady_clock::now();
            const int run_status = ppprocess::run(run);
            const auto execution_finished = std::chrono::steady_clock::now();
            if (run_status == -1) throw Error("error: could not start executable");
            if (show_timings)
                std::cerr << "timing execute     " << milliseconds(execution_started, execution_finished) << " ms\n"
                          << "timing total       " << milliseconds(process_started, execution_finished) << " ms\n";
            return run_status;
        }

        std::cout << "built " << output.string() << " [" << target_name(target) << "]\n";
        if (show_timings) std::cerr << "timing total       " << milliseconds(process_started, std::chrono::steady_clock::now()) << " ms\n";
        return 0;
    } catch (const Error &error) {
        std::cerr << error.what() << "\n";
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
