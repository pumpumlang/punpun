#ifndef PPC_DRIVER_DRIVER_HPP
#define PPC_DRIVER_DRIVER_HPP

#include <chrono>
#include <string>
#include <vector>

#include "ppc/driver/options.hpp"
#include "ppc/hir/hir.hpp"
#include "ppc/mir/mir.hpp"
#include "ppc/sema/type.hpp"
#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

/// Wall-clock time spent in one compilation phase, reported by --time-passes.
struct PhaseTiming {
    std::string name;
    double milliseconds = 0.0;
};

/// Runs the whole compilation, from command line to finished artifact.
class Driver {
  public:
    explicit Driver(const Options &options) : options_(options) {}

    /// Process exit status: 0 on success, 1 on compile error, or the program's
    /// own status under `ppc run`.
    int run();

  private:
    int command_build();
    int command_explain();
    int command_language_info();
    int command_emit_tokens();

    /// Front end: load, parse, check. Returns false when errors were reported.
    bool run_front_end(Program *&program, HirProgram *&hir);
    /// Middle: HIR to MIR, then the optimizer. Returns false on error.
    bool run_middle(const HirProgram &hir, MirProgram *&mir);
    /// Back end: generate the artifact for the selected backend. Takes the
    /// same TypeContext the checker used, because a backend has to resolve
    /// aggregate layouts and type spellings.
    bool run_backend(const MirProgram &mir, TypeContext &types, DiagnosticEngine &diagnostics,
                     std::string &artifact);
    /// Turns a C or assembly artifact into an executable.
    bool link_executable(const std::string &artifact, const std::string &output);
    /// Compiles the runtime once and caches the object file.
    /// Compiles every runtime translation unit once and caches the objects.
    ///
    /// The runtime is more than one file since the platform layer was split
    /// out, and the platform sources are whole-file guarded so exactly one of
    /// them produces code. Each is cached independently, keyed on its own
    /// contents plus the shared toolchain identity.
    bool ensure_runtime_objects(std::vector<std::string> &objects);
    /// Compiles one runtime source, or reports a cache hit.
    bool ensure_runtime_object(const std::string &runtime, const std::string &filename,
                               const std::string &shared_identity, std::string &object_path);

    /// Where the std modules live: --stdlib, then $PPC_STDLIB, then paths
    /// relative to the executable.
    std::string resolve_stdlib_directory() const;
    /// Where ppcrt.c and ppcrt.h live, resolved the same way.
    std::string resolve_runtime_directory() const;
    std::string resolve_cache_directory() const;
    /// Directory holding the running ppc binary.
    static std::string executable_directory();

    /// Runs a subprocess, echoing it first under --verbose.
    int run_command(const std::vector<std::string> &argv) const;

    void begin_phase(const char *name);
    void end_phase();
    void report_timings() const;
    void report_cache_stats() const;
    void flush_diagnostics(DiagnosticEngine &diagnostics) const;

    Options options_;
    /// Runtime-object cache accounting, reported by --cache-stats.
    mutable u32 cache_hits_ = 0;
    mutable u32 cache_misses_ = 0;
    mutable u32 cache_rebuilds_ = 0;
    mutable u32 cache_invalidations_ = 0;
    std::vector<PhaseTiming> timings_;
    std::string current_phase_;
    std::chrono::steady_clock::time_point phase_start_;
    /// Temporary files to delete on exit unless --keep was passed.
    std::vector<std::string> scratch_files_;
};

}  // namespace ppc

#endif
