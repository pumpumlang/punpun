#include "ppc/driver/driver.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include "ppcrt.h"
}

#include "ppc/codegen/c_backend.hpp"
#include "ppc/service/lsp_server.hpp"
#include "ppc/support/host.hpp"
#include "ppc/support/stable_api.hpp"
#include "ppc/support/version.hpp"
#include "ppc/codegen/vm.hpp"
#include "ppc/codegen/native_backend.hpp"
#include "ppc/codegen/vm_backend.hpp"
#include "ppc/driver/module_loader.hpp"
#include "ppc/mir/builder.hpp"
#include "ppc/mir/passes.hpp"
#include "ppc/sema/checker.hpp"
#include "ppc/syntax/lexer.hpp"

namespace ppc {

// The runtime ABI epoch exists in two places by necessity: a C header the
// generated program includes, and the C++ version header the driver reports
// from. This makes disagreement a build error rather than a silent mismatch.
static_assert(version::kRuntimeAbi == PUNPUN_RUNTIME_ABI_VERSION,
              "runtime ABI epoch disagrees between ppcrt.h and version.hpp");

namespace {

/// 128-bit content fingerprint (FNV-1a over two independent offset bases).
///
/// A 64-bit hash was previously used for cache identity. That is too narrow to
/// rely on when a collision means reusing a stale object file and silently
/// producing a wrong program: correctness here should not rest on a birthday
/// bound. Two independent lanes make an accidental collision not worth
/// reasoning about, and the cost is still one pass over a few kilobytes.
std::string fingerprint(const std::string &text) {
    std::uint64_t low = 1469598103934665603ull;
    std::uint64_t high = 0x9E3779B97F4A7C15ull;
    for (unsigned char c : text) {
        low ^= c;
        low *= 1099511628211ull;
        high ^= static_cast<std::uint64_t>(c) + 0x165667B19E3779F9ull;
        high *= 0x2545F4914F6CDD1Dull;
        high ^= high >> 29;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%016llx%016llx",
                  static_cast<unsigned long long>(low),
                  static_cast<unsigned long long>(high));
    return buffer;
}

}  // namespace

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::string Driver::executable_directory() {
    const std::string path = host::executable_path();
    return path.empty() ? std::string(".") : parent_directory(path);
}

std::string Driver::resolve_stdlib_directory() const {
    if (!options_.stdlib_dir.empty()) return options_.stdlib_dir;
    if (const char *environment = std::getenv("PPC_STDLIB")) return environment;

    // Looked for beside the binary first, then one level up, which covers both
    // an installed layout and a build tree.
    const std::string base = executable_directory();
    const std::string candidates[] = {
        join_path(base, "stdlib"),
        join_path(parent_directory(base), "stdlib"),
        join_path(parent_directory(parent_directory(base)), "stdlib"),
    };
    for (const std::string &candidate : candidates) {
        if (host::directory_exists(candidate)) return candidate;
    }
    return {};
}

std::string Driver::resolve_runtime_directory() const {
    if (!options_.runtime_dir.empty()) return options_.runtime_dir;
    if (const char *environment = std::getenv("PPC_RUNTIME")) return environment;

    const std::string base = executable_directory();
    const std::string candidates[] = {
        join_path(base, "runtime"),
        join_path(parent_directory(base), "runtime"),
        join_path(parent_directory(parent_directory(base)), "runtime"),
    };
    for (const std::string &candidate : candidates) {
        if (file_exists(join_path(candidate, "ppcrt.c"))) return candidate;
    }
    return {};
}

std::string Driver::resolve_cache_directory() const {
    if (!options_.cache_dir.empty()) return options_.cache_dir;
    return host::cache_directory();
}

// ---------------------------------------------------------------------------
// Subprocesses
// ---------------------------------------------------------------------------

int Driver::run_command(const std::vector<std::string> &argv) const {
    return host::run_process(argv, options_.verbose);
}

// ---------------------------------------------------------------------------
// Phase timing
// ---------------------------------------------------------------------------

void Driver::begin_phase(const char *name) {
    if (!options_.time_passes) return;
    end_phase();
    current_phase_ = name;
    phase_start_ = std::chrono::steady_clock::now();
}

void Driver::end_phase() {
    if (!options_.time_passes || current_phase_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double, std::milli>(now - phase_start_).count();
    timings_.push_back(PhaseTiming{current_phase_, elapsed});
    current_phase_.clear();
}

void Driver::report_cache_stats() const {
    if (!options_.cache_stats) return;
    std::fprintf(stderr, "\n  runtime object cache\n");
    std::fprintf(stderr, "  --------------------------\n");
    std::fprintf(stderr, "  hits          %u\n", cache_hits_);
    std::fprintf(stderr, "  misses        %u\n", cache_misses_);
    std::fprintf(stderr, "  rebuilds      %u\n", cache_rebuilds_);
    std::fprintf(stderr, "  invalidations %u\n", cache_invalidations_);
    std::fprintf(stderr, "  directory     %s\n", resolve_cache_directory().c_str());
}

void Driver::report_timings() const {
    if (!options_.time_passes) return;
    double total = 0.0;
    std::fprintf(stderr, "\n  phase                     time\n");
    std::fprintf(stderr, "  ----------------------------------\n");
    for (const PhaseTiming &timing : timings_) {
        std::fprintf(stderr, "  %-22s %7.2f ms\n", timing.name.c_str(), timing.milliseconds);
        total += timing.milliseconds;
    }
    std::fprintf(stderr, "  ----------------------------------\n");
    std::fprintf(stderr, "  %-22s %7.2f ms\n", "total", total);
}

void Driver::flush_diagnostics(DiagnosticEngine &diagnostics) const {
    if (diagnostics.all().empty()) return;
    const std::string rendered =
        options_.json_diagnostics ? diagnostics.render_json() : diagnostics.render();
    std::fputs(rendered.c_str(), stderr);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

int Driver::command_explain() {
    // The full text lives in docs/diagnostics.md so the compiler and the manual
    // cannot disagree.
    const std::string base = executable_directory();
    const std::string candidates[] = {
        join_path(join_path(base, "docs"), "diagnostics.md"),
        join_path(join_path(parent_directory(base), "docs"), "diagnostics.md"),
        join_path(join_path(parent_directory(parent_directory(base)), "docs"), "diagnostics.md"),
    };

    std::string text;
    bool found = false;
    for (const std::string &candidate : candidates) {
        if (read_file(candidate, text)) { found = true; break; }
    }
    if (!found) {
        std::fprintf(stderr, "ppc: cannot find docs/diagnostics.md\n");
        return 1;
    }

    // Each code is a `## E0800 — title` heading; print from that heading to the
    // next one.
    std::string wanted = options_.explain_code;
    for (char &c : wanted) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    const std::string marker = "## " + wanted;
    const std::size_t start = text.find(marker);
    if (start == std::string::npos) {
        std::fprintf(stderr, "ppc: no documentation for '%s'\n", wanted.c_str());
        return 1;
    }
    std::size_t end = text.find("\n## ", start + 1);
    if (end == std::string::npos) end = text.size();
    std::fputs(text.substr(start, end - start).c_str(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

int Driver::command_language_info() {
    // Required by spec/1.0/abi-and-packages.md. Emitted as JSON because the
    // consumer is a build system, not a person.
    //
    // `language` is what PPC accepts; `implementation_status` is how much of it
    // is actually implemented. Reporting only the first would let a tool assume
    // parity that does not exist.
    std::printf("{\n");
    std::printf("  \"compiler\": \"ppc\",\n");
    std::printf("  \"compiler_version\": \"%s\",\n", version::kCompilerVersion);
    std::printf("  \"language\": \"%s\",\n", version::kLanguageVersion);
    std::printf("  \"language_version\": \"%s\",\n", version::kLanguageVersion);
    std::printf("  \"language_stability\": \"stable\",\n");
    std::printf("  \"implementation_status\": \"%s\",\n", version::kImplementationStatus);
    std::printf("  \"language_abi\": %d,\n", version::kLanguageAbi);
    std::printf("  \"abi_version\": %d,\n", version::kLanguageAbi);
    std::printf("  \"runtime_abi\": %d,\n", version::kRuntimeAbi);
    std::printf("  \"runtime_abi_version\": %d,\n", version::kRuntimeAbi);
    std::printf("  \"package_format\": %d,\n", version::kPackageFormat);
    std::printf("  \"lockfile_format\": %d,\n", version::kLockfileFormat);
    std::printf("  \"cache_epoch\": %d,\n", version::kCacheEpoch);
    // One grammar. The migration forms still parse so 0.6 source keeps
    // building, but they warn and are not part of the language being taught.
    std::printf("  \"dialect\": \"modern\",\n");
    std::printf("  \"deprecated_dialects\": [\"migration\"],\n");
    std::printf("  \"backends\": [\"c\", \"native\", \"bytecode\"],\n");
    std::printf("  \"checked_arithmetic\": true,\n");
    std::printf("  \"keywords\": %s,\n", version::kStableKeywordsJson);
    std::printf("  \"builtins\": [\n");
    const auto &builtins = builtin_table();
    auto type_name = [](BuiltinType type) -> const char * {
        switch (type) {
            case BuiltinType::Void: return "void";
            case BuiltinType::Bool: return "bool";
            case BuiltinType::Int: return "int";
            case BuiltinType::Float: return "float";
            case BuiltinType::Str: return "str";
            case BuiltinType::Nums: return "nums";
            case BuiltinType::IntSlice: return "Slice<int>";
            case BuiltinType::Bytes: return "bytes";
            case BuiltinType::ListOfStr: return "List<str>";
            case BuiltinType::AnyList: return "List<T>";
            case BuiltinType::ListElement: return "T";
            case BuiltinType::NewList: return "List<T>";
            case BuiltinType::AnyMap: return "Map<T>";
            case BuiltinType::MapValue: return "T";
            case BuiltinType::NewMap: return "Map<T>";
            case BuiltinType::AnyTask:
            case BuiltinType::AnyScalar:
            case BuiltinType::SameAsArgument: return "inferred";
        }
        return "inferred";
    };
    for (std::size_t index = 0; index < builtins.size(); ++index) {
        const BuiltinSpec &builtin = builtins[index];
        std::printf("    {\"name\":\"%s\",\"parameters\":[", builtin.name);
        for (std::size_t parameter = 0; parameter < builtin.params.size(); ++parameter) {
            if (parameter) std::printf(",");
            std::printf("\"%s\"", type_name(builtin.params[parameter]));
        }
        std::printf("],\"result\":\"%s\"}%s\n", type_name(builtin.result),
                    index + 1 == builtins.size() ? "" : ",");
    }
    std::printf("  ],\n");
    std::printf("  \"stdlib_symbols\": %s\n", version::kStableStdlibJson);
    std::printf("}\n");
    return 0;
}

int Driver::command_emit_tokens() {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    diagnostics.set_color(options_.color);
    diagnostics.set_error_limit(options_.error_limit);
    Interner interner;

    auto file = sources.load(options_.inputs.front());
    if (!file) {
        std::fprintf(stderr, "ppc: cannot open '%s'\n", options_.inputs.front().c_str());
        return 1;
    }

    Lexer lexer(sources, diagnostics, interner);
    const std::vector<Token> tokens = lexer.tokenize(*file);

    for (const Token &token : tokens) {
        const LineColumn position = sources.locate(token.span.file, token.span.start);
        std::printf("%4u:%-4u %-20s", position.line, position.column,
                    token_spelling(token.kind));
        switch (token.kind) {
            case Tok::Identifier:
            case Tok::StringLiteral:
                std::printf("  %s", interner.text(token.text).c_str());
                break;
            case Tok::IntLiteral: std::printf("  %lld", static_cast<long long>(token.int_value)); break;
            case Tok::FloatLiteral: std::printf("  %g", token.float_value); break;
            case Tok::Semicolon:
                if (token.synthetic) std::printf("  (from newline)");
                break;
            default: break;
        }
        std::printf("\n");
    }

    flush_diagnostics(diagnostics);
    return diagnostics.has_errors() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

int Driver::run() {
    switch (options_.command) {
        case Command::Help: std::fputs(help_text().c_str(), stdout); return 0;
        case Command::Version: std::fputs(version_text().c_str(), stdout); return 0;
        case Command::Explain: return command_explain();
        case Command::LanguageInfo: return command_language_info();
        case Command::Serve: {
            service::LspServer server;
            const std::string stdlib = resolve_stdlib_directory();
            if (!stdlib.empty()) server.add_search_path(stdlib);
            for (const std::string &input : options_.inputs) {
                server.add_search_path(parent_directory(normalize_path(input)));
            }
            return server.run();
        }
        case Command::EmitTokens: return command_emit_tokens();
        default: return command_build();
    }
}

int Driver::command_build() {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    diagnostics.set_color(options_.color);
    diagnostics.set_error_limit(options_.error_limit);

    Interner interner;
    Arena arena;
    TypeContext types(interner);

    const std::string input = options_.inputs.front();

    // -- front end -----------------------------------------------------------
    begin_phase("parse");
    ModuleLoader loader(sources, arena, interner, diagnostics);
    loader.add_search_path(parent_directory(normalize_path(input)));
    for (const std::string &path : options_.module_paths) loader.add_search_path(path);
    const std::string stdlib = resolve_stdlib_directory();
    if (!stdlib.empty()) loader.add_search_path(stdlib);

    Program *program = loader.load(input);
    if (!program || diagnostics.has_errors()) {
        end_phase();
        flush_diagnostics(diagnostics);
        report_timings();
        return 1;
    }

    if (options_.command == Command::EmitAst) {
        end_phase();
        std::printf("parsed %zu module(s)\n", program->modules.size());
        for (const Module *module : program->modules) {
            std::printf("\nmodule %s\n", module->path.c_str());
            std::printf("  imports: %zu  functions: %zu  types: %zu  enums: %zu  contracts: %zu\n",
                        module->imports.size(), module->functions.size(), module->shapes.size(),
                        module->enums.size(), module->contracts.size());
            for (const FunctionDecl *fn : module->functions) {
                std::printf("    fn %s", fn->qualified_name(interner).c_str());
                if (!fn->generics.empty()) {
                    std::printf("<");
                    for (std::size_t i = 0; i < fn->generics.size(); ++i) {
                        if (i) std::printf(", ");
                        std::printf("%s", interner.text(fn->generics[i].name).c_str());
                    }
                    std::printf(">");
                }
                std::printf("/%zu%s\n", fn->params.size(), fn->is_entry ? "  [entry]" : "");
            }
            for (const ShapeDecl *shape : module->shapes) {
                std::printf("    %s %s  fields:%zu methods:%zu\n",
                            shape->is_reference ? "object" : "struct",
                            interner.text(shape->name).c_str(), shape->fields.size(),
                            shape->methods.size());
            }
            for (const EnumDecl *decl : module->enums) {
                std::printf("    enum %s  variants:%zu\n", interner.text(decl->name).c_str(),
                            decl->variants.size());
            }
        }
        report_timings();
        return 0;
    }

    begin_phase("check");
    Checker checker(types, arena, interner, diagnostics);
    HirProgram *hir = checker.check(*program);
    end_phase();

    flush_diagnostics(diagnostics);
    if (diagnostics.has_errors()) {
        report_timings();
        return 1;
    }

    if (options_.command == Command::Check) {
        report_timings();
        if (!options_.json_diagnostics) {
            std::printf("ok: %zu function(s) checked\n", hir->functions.size());
        }
        return 0;
    }

    if (options_.command == Command::EmitHir) {
        for (const HirFunction *fn : hir->functions) {
            std::printf("fn %s -> %s%s\n", fn->name.c_str(), types.describe(fn->result).c_str(),
                        fn->is_entry ? "  ; entry" : "");
            for (std::size_t i = 0; i < fn->locals.size(); ++i) {
                std::printf("  local $%zu %-12s : %s%s\n", i,
                            interner.text(fn->locals[i].name).c_str(),
                            types.describe(fn->locals[i].type).c_str(),
                            i < fn->param_count ? "  ; parameter" : "");
            }
            std::printf("  %zu top-level statement(s)\n\n", fn->body.size());
        }
        report_timings();
        return 0;
    }

    // -- middle --------------------------------------------------------------
    begin_phase("lower to MIR");
    MirBuilder builder(types, arena, diagnostics);
    MirProgram *mir = builder.build(*hir);
    end_phase();

    if (diagnostics.has_errors()) {
        flush_diagnostics(diagnostics);
        report_timings();
        return 1;
    }

    begin_phase("optimize");
    const PassStats stats = optimize(*mir, types, options_.opt);
    end_phase();

    if (options_.verify_mir) {
        for (const MirFunction *fn : mir->functions) {
            if (fn->is_extern_native) continue;
            const std::string problem = fn->verify();
            if (problem.empty()) continue;
            diagnostics
                .error(Code::BackendInternal, "MIR verification failed in '" + fn->name + "'")
                .note(problem)
                .note("this is a compiler bug; please report it");
        }
        if (diagnostics.has_errors()) {
            flush_diagnostics(diagnostics);
            return 1;
        }
    }

    if (options_.show_stats) std::fputs(stats.summary().c_str(), stderr);

    if (options_.command == Command::EmitMir) {
        std::fputs(dump_mir(*mir, types).c_str(), stdout);
        report_timings();
        return 0;
    }

    // -- back end ------------------------------------------------------------
    // The bytecode path skips text generation, the host compiler, and the
    // linker entirely: for `run` it goes straight from MIR to the interpreter,
    // which is what makes it the fastest option from source to output.
    if (options_.backend == BackendKind::Bytecode &&
        (options_.command == Command::Run || options_.command == Command::EmitBackend)) {
        begin_phase("codegen");
        VmBackend backend(types, diagnostics);
        BytecodeProgram bytecode;
        const bool ok = backend.compile(*mir, [&] {
            CodegenOptions codegen;
            codegen.opt = options_.opt;
            codegen.unchecked_arithmetic = options_.unchecked_arithmetic;
            codegen.debug_info = options_.debug_info;
            codegen.source_name = input;
            return codegen;
        }(), bytecode);
        end_phase();

        flush_diagnostics(diagnostics);
        if (!ok) {
            report_timings();
            return 1;
        }

        if (options_.command == Command::EmitBackend) {
            std::fputs(bytecode.disassemble().c_str(), stdout);
            report_timings();
            return 0;
        }

        report_timings();

        // The interpreter is handed the program's own arguments, so `arg` and
        // `arg_count` behave the same as under a compiled binary.
        std::vector<std::string> storage{input};
        for (const std::string &argument : options_.program_arguments) {
            storage.push_back(argument);
        }
        std::vector<char *> raw;
        raw.reserve(storage.size() + 1);
        for (std::string &argument : storage) raw.push_back(argument.data());
        raw.push_back(nullptr);

        Vm vm;
        const int status = vm.run(bytecode, static_cast<int>(storage.size()), raw.data());
        if (!vm.error().empty()) {
            std::fprintf(stderr, "ppc: %s\n", vm.error().c_str());
            return 1;
        }
        return status;
    }

    begin_phase("codegen");
    std::string artifact;
    const bool generated = run_backend(*mir, types, diagnostics, artifact);
    end_phase();

    flush_diagnostics(diagnostics);
    if (!generated || diagnostics.has_errors()) {
        report_timings();
        return 1;
    }

    if (options_.command == Command::EmitBackend) {
        std::fputs(artifact.c_str(), stdout);
        report_timings();
        return 0;
    }

    // -- link ----------------------------------------------------------------
    std::string output = options_.output;
    if (output.empty()) {
        output = path_stem(input);
        if (options_.command == Command::Run) {
            // A pid-based name is not unique: pids recycle, and two builds of
            // the same program race. An exclusively-created temp name cannot
            // collide, and the exe suffix keeps the artifact runnable on hosts
            // that require one.
            output = host::create_temp_file("ppc-run-" + output, host::executable_suffix());
            if (output.empty()) {
                std::fprintf(stderr, "ppc: cannot create a temporary file\n");
                return 1;
            }
            scratch_files_.push_back(output);
        }
    }

    if (options_.backend == BackendKind::Bytecode) {
        // There is nothing to link: the image is the deliverable, and `ppc run`
        // interprets it.
        const std::string image = output.size() > 4 && output.compare(output.size() - 4, 4, ".ppb") == 0
                                      ? output
                                      : output + ".ppb";
        if (!write_file(image, artifact)) {
            std::fprintf(stderr, "ppc: cannot write '%s'\n", image.c_str());
            return 1;
        }
        report_timings();
        if (options_.verbose) std::fprintf(stderr, "ppc: wrote %s\n", image.c_str());
        return 0;
    }

    begin_phase("link");
    const bool linked = link_executable(artifact, output);
    end_phase();

    flush_diagnostics(diagnostics);
    if (!linked) {
        report_timings();
        return 1;
    }
    report_timings();

    if (options_.command == Command::Run) {
        std::vector<std::string> argv{output};
        for (const std::string &argument : options_.program_arguments) argv.push_back(argument);
        const int status = run_command(argv);
        if (!options_.keep_intermediates) {
            for (const std::string &path : scratch_files_) host::remove_file(path);
        }
        report_cache_stats();
        return status;
    }

    if (!options_.keep_intermediates) {
        for (const std::string &path : scratch_files_) host::remove_file(path);
    }
    report_cache_stats();
    return 0;
}

bool Driver::run_backend(const MirProgram &mir, TypeContext &types,
                         DiagnosticEngine &diagnostics, std::string &artifact) {
    CodegenOptions codegen;
    codegen.opt = options_.opt;
    codegen.unchecked_arithmetic = options_.unchecked_arithmetic;
    codegen.debug_info = options_.debug_info;
    codegen.source_name = options_.inputs.empty() ? std::string() : options_.inputs.front();

    switch (options_.backend) {
        case BackendKind::C: {
            CBackend backend(types, diagnostics);
            return backend.emit(mir, codegen, artifact);
        }
        case BackendKind::Bytecode: {
            VmBackend backend(types, diagnostics);
            return backend.emit(mir, codegen, artifact);
        }
        case BackendKind::Native: {
            NativeBackend backend(types, diagnostics);
            return backend.emit(mir, codegen, artifact);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Linking
// ---------------------------------------------------------------------------

bool Driver::ensure_runtime_objects(std::vector<std::string> &objects) {
    const std::string runtime = resolve_runtime_directory();
    if (runtime.empty()) {
        std::fprintf(stderr,
                     "ppc: cannot find the runtime (ppcrt.c). Pass --runtime DIR or set "
                     "PPC_RUNTIME.\n");
        return false;
    }

    // Identity shared by every runtime object. Anything omitted here becomes a
    // way to silently reuse a wrong artifact, so the bias is to over-include: a
    // redundant input costs one rebuild, a missing one costs a miscompiled
    // program.
    std::string header_text;
    read_file(join_path(runtime, "ppcrt.h"), header_text);
    std::string platform_header;
    read_file(join_path(runtime, "ppc_platform.h"), platform_header);

    std::string shared;
    shared += "epoch="; shared += std::to_string(version::kCacheEpoch);
    shared += "|abi="; shared += std::to_string(version::kRuntimeAbi);
    shared += "|ppc="; shared += version::kCompilerVersion;
    shared += "|target="; shared += host::platform_name();
    shared += "-"; shared += host::architecture_name();
    shared += "|cc="; shared += options_.host_compiler;
    // The command name alone is not enough: `cc` can become a different
    // compiler after a system upgrade while keeping its name.
    shared += "|ccid="; shared += host::toolchain_identity(options_.host_compiler);
    shared += "|flags=-std=c11 -O2 -fPIC";
    shared += "|hdr="; shared += header_text;
    shared += "|plat="; shared += platform_header;

    // Both platform implementations are compiled. The one that does not match
    // the host is guarded down to an empty translation unit, which costs a
    // near-instant compile and keeps this list free of platform conditionals.
    static const char *const kRuntimeSources[] = {
        "ppcrt.c", "ppc_https.c", "ppc_gui.c", "ppc_net.c",
        "ppc_platform_posix.c", "ppc_platform_windows.c"};

    objects.clear();
    for (const char *filename : kRuntimeSources) {
        if (!file_exists(join_path(runtime, filename))) continue;
        std::string object;
        if (!ensure_runtime_object(runtime, filename, shared, object)) return false;
        objects.push_back(object);
    }

    if (objects.empty()) {
        std::fprintf(stderr, "ppc: the runtime directory '%s' contains no sources\n",
                     runtime.c_str());
        return false;
    }
    return true;
}

bool Driver::ensure_runtime_object(const std::string &runtime, const std::string &filename,
                                   const std::string &shared_identity,
                                   std::string &object_path) {
    const std::string source = join_path(runtime, filename);
    std::string text;
    if (!read_file(source, text)) {
        std::fprintf(stderr, "ppc: cannot read '%s'\n", source.c_str());
        return false;
    }

    if (options_.disable_cache) {
        object_path = host::create_temp_file("ppc-rt", ".o");
        if (object_path.empty()) {
            std::fprintf(stderr, "ppc: cannot create a temporary runtime object\n");
            return false;
        }
        scratch_files_.push_back(object_path);
        const std::vector<std::string> command{
            options_.host_compiler, "-std=c11", "-O2", "-fPIC", "-c", source,
            "-o", object_path};
        ++cache_misses_;
        ++cache_rebuilds_;
        if (host::run_process(command, options_.verbose) != 0) {
            host::remove_file(object_path);
            std::fprintf(stderr, "ppc: failed to compile the runtime source '%s'\n",
                         filename.c_str());
            return false;
        }
        return true;
    }

    const std::string cache = resolve_cache_directory();
    if (!host::make_directories(cache)) {
        std::fprintf(stderr, "ppc: cannot create the cache directory '%s'\n", cache.c_str());
        return false;
    }

    const std::string key = fingerprint(shared_identity + "|file=" + filename + "|src=" + text);
    object_path = join_path(cache, "ppcrt-" + path_stem(filename) + "-" + key + ".o");

    if (file_exists(object_path)) {
        // A zero-length object means a previous run was interrupted between
        // creating the file and finishing the write. Rebuilding is always safe;
        // trusting it is not.
        if (ppc::file_size(object_path) > 0) {
            ++cache_hits_;
            return true;
        }
        host::remove_file(object_path);
        ++cache_invalidations_;
    }
    ++cache_misses_;

    // Compile to a unique temporary and rename into place. Two concurrent
    // builds may both compile; whichever finishes last wins, and neither can
    // observe a half-written object, because rename is atomic within a
    // filesystem.
    const std::string staging = host::create_temp_file("ppc-rt", ".o");
    if (staging.empty()) {
        std::fprintf(stderr, "ppc: cannot create a temporary file for the runtime\n");
        return false;
    }

    const std::vector<std::string> command{
        options_.host_compiler, "-std=c11", "-O2", "-fPIC", "-c", source, "-o", staging};
    if (host::run_process(command, options_.verbose) != 0) {
        host::remove_file(staging);
        std::fprintf(stderr, "ppc: failed to compile the runtime source '%s'\n",
                     filename.c_str());
        return false;
    }

    if (!host::rename_file(staging, object_path)) {
        // Losing the race is fine as long as some correct object is in place.
        host::remove_file(staging);
        if (!file_exists(object_path)) {
            std::fprintf(stderr, "ppc: cannot install the compiled runtime\n");
            return false;
        }
    }
    return true;
}

bool Driver::link_executable(const std::string &artifact, const std::string &output) {
    std::vector<std::string> runtime_objects;
    if (!ensure_runtime_objects(runtime_objects)) return false;

    const std::string runtime = resolve_runtime_directory();
    // The native backend emits assembly rather than C; the host driver picks the
    // assembler or the C compiler from the extension, so only the suffix
    // changes here.
    const char *extension = (options_.backend == BackendKind::Native) ? ".s" : ".c";
    const std::string generated = host::create_temp_file("ppc-gen", extension);
    if (generated.empty()) {
        std::fprintf(stderr, "ppc: cannot create a temporary file for generated code\n");
        return false;
    }

    if (!write_file(generated, artifact)) {
        std::fprintf(stderr, "ppc: cannot write '%s'\n", generated.c_str());
        return false;
    }
    if (!options_.keep_intermediates) scratch_files_.push_back(generated);

    std::vector<std::string> command{options_.host_compiler};
    if (options_.backend == BackendKind::Native) {
        // Assembly needs no optimization flags: the code is already final, and
        // this is exactly why the native path compiles faster than going
        // through C.
    } else {
        command.push_back("-std=c11");
        // -O2 on the generated C is where most of its runtime performance comes
        // from; ppc's own optimizer runs before this and does not duplicate it.
        command.push_back(options_.opt == OptLevel::None ? "-O0" : "-O2");
    }
    if (options_.debug_info) command.push_back("-g");
    command.push_back("-I");
    command.push_back(runtime);
    command.push_back(generated);
    for (const std::string &object : runtime_objects) command.push_back(object);
    command.push_back("-o");
    command.push_back(output);
    command.push_back("-lpthread");
    command.push_back("-lm");
#if defined(__linux__)
    command.push_back("-ldl");
#elif defined(_WIN32)
    command.push_back("-luser32");
    command.push_back("-lgdi32");
    command.push_back("-lws2_32");
#endif

    if (run_command(command) != 0) {
        std::fprintf(stderr, "ppc: the host toolchain failed on the generated %s\n",
                     options_.backend == BackendKind::Native ? "assembly" : "C");
        if (!options_.keep_intermediates) {
            std::fprintf(stderr, "ppc: rerun with --keep to inspect %s\n", generated.c_str());
        }
        return false;
    }
    return true;
}

}  // namespace ppc
