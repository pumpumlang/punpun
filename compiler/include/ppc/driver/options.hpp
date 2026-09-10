#ifndef PPC_DRIVER_OPTIONS_HPP
#define PPC_DRIVER_OPTIONS_HPP

#include <string>
#include <vector>

#include "ppc/codegen/backend.hpp"
#include "ppc/mir/passes.hpp"

namespace ppc {

enum class Command {
    Build,     // compile to an executable
    Run,       // compile, then execute
    Check,     // front end only, no code generation
    EmitTokens,
    EmitAst,
    EmitHir,
    EmitMir,
    EmitBackend,  // the backend's own artifact: C source, assembly, or bytecode
    Explain,      // describe a diagnostic code
    LanguageInfo, // report the language, runtime, package, and lockfile epochs
    Serve,        // run as a Language Server Protocol server over stdio
    Version,
    Help,
};

struct Options {
    Command command = Command::Build;

    std::vector<std::string> inputs;
    std::vector<std::string> module_paths;
    /// Arguments passed through to the program under `ppc run`.
    std::vector<std::string> program_arguments;

    std::string output;        // -o
    std::string stdlib_dir;    // --stdlib
    std::string runtime_dir;   // --runtime
    std::string cache_dir;     // --cache-dir
    std::string explain_code;  // argument to `explain`

    BackendKind backend = BackendKind::C;
    OptLevel opt = OptLevel::Basic;
    /// Host C compiler used by the C backend and for linking.
    std::string host_compiler = "cc";

    bool unchecked_arithmetic = false;
    bool debug_info = false;
    bool keep_intermediates = false;
    bool color = true;
    bool json_diagnostics = false;
    bool time_passes = false;
    bool show_stats = false;
    bool cache_stats = false;
    bool disable_cache = false;
    bool verbose = false;
    bool verify_mir = false;
    std::size_t error_limit = 100;

    /// True when the requested command stops before code generation.
    bool front_end_only() const {
        switch (command) {
            case Command::Check:
            case Command::EmitTokens:
            case Command::EmitAst:
            case Command::EmitHir:
            case Command::EmitMir:
                return true;
            default:
                return false;
        }
    }
};

/// Parses the command line. Returns false and prints a message on bad input.
bool parse_options(int argc, char **argv, Options &out, std::string &error);

/// Full `--help` text.
std::string help_text();
/// Version banner.
std::string version_text();

}  // namespace ppc

#endif
