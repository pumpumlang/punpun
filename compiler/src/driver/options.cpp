#include "ppc/driver/options.hpp"

#include "ppc/support/version.hpp"

#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace ppc {

namespace {

/// Splits `--name=value` into its parts. Returns false for a bare flag.
bool split_assignment(const std::string &argument, std::string &name, std::string &value) {
    const std::size_t position = argument.find('=');
    if (position == std::string::npos) return false;
    name = argument.substr(0, position);
    value = argument.substr(position + 1);
    return true;
}

bool parse_command(const std::string &word, Command &out) {
    if (word == "build") { out = Command::Build; return true; }
    if (word == "run" || word == "go") { out = Command::Run; return true; }
    if (word == "check") { out = Command::Check; return true; }
    if (word == "emit-tokens") { out = Command::EmitTokens; return true; }
    if (word == "emit-ast") { out = Command::EmitAst; return true; }
    if (word == "emit-hir") { out = Command::EmitHir; return true; }
    if (word == "emit-mir" || word == "emit-ir" || word == "emit-machine-ir") {
        out = Command::EmitMir;
        return true;
    }
    if (word == "emit-c" || word == "emit-asm" || word == "emit-bytecode" ||
        word == "emit-backend") {
        out = Command::EmitBackend;
        return true;
    }
    if (word == "explain") { out = Command::Explain; return true; }
    if (word == "language-info") { out = Command::LanguageInfo; return true; }
    if (word == "serve") { out = Command::Serve; return true; }
    if (word == "version" || word == "--version" || word == "-V") {
        out = Command::Version;
        return true;
    }
    if (word == "help" || word == "--help" || word == "-h") { out = Command::Help; return true; }
    return false;
}

}  // namespace

std::string version_text() {
    // Assembled from the version header rather than written out, so a release
    // cannot bump one number and forget another.
    std::ostringstream out;
    out << "ppc " << version::kCompilerVersion << "\n";
    return out.str();
}

std::string help_text() {
    return
        "ppc — the PunPun compiler\n"
        "\n"
        "USAGE\n"
        "    ppc <command> [options] <file.pp> [-- program arguments]\n"
        "\n"
        "COMMANDS\n"
        "    build            compile to a native executable\n"
        "    run              compile, then run the result\n"
        "    check            type-check only, emit no code\n"
        "    emit-tokens      dump the token stream\n"
        "    emit-ast         dump the parsed syntax tree\n"
        "    emit-hir         dump typed, monomorphized HIR\n"
        "    emit-mir         dump the optimized MIR control-flow graph\n"
        "    emit-c           dump the backend artifact (C, assembly, or bytecode)\n"
        "    explain <CODE>   describe a diagnostic code, e.g. `ppc explain E0800`\n"
        "    language-info    report language, runtime, package and lockfile epochs\n"
        "    serve --stdio    run as an LSP server for editor integration\n"
        "    version          print the version\n"
        "    help             print this message\n"
        "\n"
        "BACKENDS  (--backend=NAME)\n"
        "    c                emit C and compile it with the host toolchain.\n"
        "                     Slowest to compile, fastest to run.  [default]\n"
        "    native           emit x86-64 assembly directly.\n"
        "                     Fast to compile, good runtime performance.\n"
        "    bytecode         emit bytecode for the bundled VM.\n"
        "                     Fastest to compile, slowest to run, no toolchain needed.\n"
        "\n"
        "OPTIMIZATION\n"
        "    -O0              no optimization; fastest possible compile\n"
        "    -O1              cheap local cleanups                        [default]\n"
        "    -O2              full pipeline, iterated to a fixed point\n"
        "    --unchecked      use raw machine arithmetic instead of checked helpers.\n"
        "                     PunPun specifies checked integer arithmetic, so this\n"
        "                     changes observable behavior on overflow.\n"
        "\n"
        "OPTIONS\n"
        "    -o PATH          output path\n"
        "    --stdlib DIR     directory holding the std modules\n"
        "    --module-path DIR add an import search root (repeatable)\n"
        "    --runtime DIR    directory holding ppcrt.c and ppcrt.h\n"
        "    --cache-dir DIR  where to cache the compiled runtime\n"
        "    --no-cache       rebuild runtime objects for this invocation\n"
        "    --cc PROGRAM     host C compiler to use          [default: cc]\n"
        "    -g               emit line directives for debugging\n"
        "    --keep           keep intermediate files\n"
        "    --json           emit diagnostics as JSON\n"
        "    --no-color       disable colored diagnostics\n"
        "    --time-passes    report time spent in each phase\n"
        "    --stats          report what the optimizer changed\n"
        "    --cache-stats    report runtime-object cache hits, misses and rebuilds\n"
        "    --verify         run the MIR verifier after every pass\n"
        "    --error-limit N  stop after N errors               [default: 100]\n"
        "    -v, --verbose    show the commands ppc runs\n"
        "\n"
        "EXAMPLES\n"
        "    ppc run hello.pp\n"
        "    ppc build -O2 --backend=c -o game main.pp\n"
        "    ppc run --backend=bytecode script.pp -- --limit 500\n"
        "    ppc emit-mir -O2 sieve.pp\n";
}

bool parse_options(int argc, char **argv, Options &out, std::string &error) {
    if (argc < 2) {
        out.command = Command::Help;
        return true;
    }

    int index = 1;
    // The first word is the command unless it looks like a file or a flag, in
    // which case `build` is assumed so `ppc main.pp` works.
    if (!parse_command(argv[1], out.command)) {
        out.command = Command::Build;
    } else {
        if (std::string(argv[1]) == "emit-asm") out.backend = BackendKind::Native;
        if (std::string(argv[1]) == "emit-bytecode") out.backend = BackendKind::Bytecode;
        ++index;
    }

    if (out.command == Command::Explain) {
        if (index >= argc) {
            error = "`ppc explain` needs a diagnostic code, for example E0800";
            return false;
        }
        out.explain_code = argv[index];
        return true;
    }

    bool after_separator = false;
    for (; index < argc; ++index) {
        const std::string argument = argv[index];

        // Everything after `--` belongs to the program being run.
        if (!after_separator && argument == "--") {
            after_separator = true;
            continue;
        }
        if (after_separator) {
            out.program_arguments.push_back(argument);
            continue;
        }

        auto next_value = [&](const char *flag) -> std::string {
            if (index + 1 >= argc) {
                error = std::string(flag) + " needs a value";
                return {};
            }
            return argv[++index];
        };

        std::string name;
        std::string value;
        if (split_assignment(argument, name, value)) {
            if (name == "--backend") {
                if (!parse_backend(value, out.backend)) {
                    error = "unknown backend '" + value + "'; choose c, native, or bytecode";
                    return false;
                }
                continue;
            }
            if (name == "--stdlib") { out.stdlib_dir = value; continue; }
            if (name == "--module-path") { out.module_paths.push_back(value); continue; }
            if (name == "--runtime") { out.runtime_dir = value; continue; }
            if (name == "--cache-dir") { out.cache_dir = value; continue; }
            if (name == "--cc") { out.host_compiler = value; continue; }
            if (name == "--error-limit") {
                out.error_limit = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
                continue;
            }
            if (name == "-o" || name == "--output") { out.output = value; continue; }
            error = "unknown option '" + name + "'";
            return false;
        }

        if (argument == "-o") {
            out.output = next_value("-o");
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--stdlib") {
            out.stdlib_dir = next_value("--stdlib");
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--module-path") {
            out.module_paths.push_back(next_value("--module-path"));
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--runtime") {
            out.runtime_dir = next_value("--runtime");
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--cache-dir") {
            out.cache_dir = next_value("--cache-dir");
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--cc") {
            out.host_compiler = next_value("--cc");
            if (!error.empty()) return false;
            continue;
        }
        if (argument == "--backend") {
            const std::string chosen = next_value("--backend");
            if (!error.empty()) return false;
            if (!parse_backend(chosen, out.backend)) {
                error = "unknown backend '" + chosen + "'; choose c, native, or bytecode";
                return false;
            }
            continue;
        }
        if (argument == "--error-limit") {
            const std::string limit = next_value("--error-limit");
            if (!error.empty()) return false;
            out.error_limit = static_cast<std::size_t>(std::strtoul(limit.c_str(), nullptr, 10));
            continue;
        }

        if (argument == "-O0") { out.opt = OptLevel::None; continue; }
        if (argument == "-O1" || argument == "-O") { out.opt = OptLevel::Basic; continue; }
        if (argument == "-O2" || argument == "-O3") { out.opt = OptLevel::Full; continue; }
        if (argument == "--release") { out.opt = OptLevel::Full; continue; }
        if (argument == "--cc-backend") { out.backend = BackendKind::C; continue; }
        if (argument == "--native-backend" || argument == "--direct-backend") {
            out.backend = BackendKind::Native;
            continue;
        }
        if (argument == "--unchecked") { out.unchecked_arithmetic = true; continue; }
        if (argument == "-g") { out.debug_info = true; continue; }
        if (argument == "--keep") { out.keep_intermediates = true; continue; }
        if (argument == "--json") { out.json_diagnostics = true; continue; }
        // Accepted for symmetry with other language servers. stdio is the only
        // transport, so the flag is a no-op rather than an error.
        if (argument == "--stdio") { continue; }
        if (argument == "--no-color") { out.color = false; continue; }
        if (argument == "--time-passes") { out.time_passes = true; continue; }
        if (argument == "--stats") { out.show_stats = true; continue; }
        if (argument == "--cache-stats" || argument == "--cache-info") {
            out.cache_stats = true;
            continue;
        }
        if (argument == "--no-cache") { out.disable_cache = true; continue; }
        if (argument == "--verify") { out.verify_mir = true; continue; }
        if (argument == "-v" || argument == "--verbose") { out.verbose = true; continue; }
        if (argument == "-h" || argument == "--help") { out.command = Command::Help; return true; }
        if (argument == "-V" || argument == "--version") {
            out.command = Command::Version;
            return true;
        }

        if (!argument.empty() && argument[0] == '-') {
            error = "unknown option '" + argument + "'";
            return false;
        }
        out.inputs.push_back(argument);
    }

    // Colors are meaningless when the output is redirected or machine-read.
    if (!isatty(2)) out.color = false;
    if (out.json_diagnostics) out.color = false;

    if (out.inputs.empty() && out.command != Command::Help &&
        out.command != Command::Version && out.command != Command::LanguageInfo &&
        out.command != Command::Serve) {
        error = "no input file; run `ppc help` for usage";
        return false;
    }
    return true;
}

}  // namespace ppc
