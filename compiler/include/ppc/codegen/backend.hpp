#ifndef PPC_CODEGEN_BACKEND_HPP
#define PPC_CODEGEN_BACKEND_HPP

#include <string>
#include <vector>

#include "ppc/mir/mir.hpp"
#include "ppc/mir/passes.hpp"
#include "ppc/support/diagnostic.hpp"

namespace ppc {

/// Which code generator to run. This is the "multiple options" lever: all three
/// share the same front end, checker, and optimizer, and differ only in what
/// they emit and therefore in where they sit on the compile-time / run-time
/// trade-off.
enum class BackendKind {
    /// Emits C and hands it to the host compiler. Slowest to compile, fastest
    /// to run, and portable to any target with a C toolchain.
    C,
    /// Emits x86-64 assembly directly and assembles it. Much faster to compile
    /// than going through C, with good but not optimal runtime performance.
    Native,
    /// Emits bytecode for the bundled VM. Near-instant compile, no external
    /// toolchain needed at all, and the slowest at runtime.
    Bytecode,
};

const char *backend_name(BackendKind kind);
/// Parses a `--backend=` value. Returns false when the name is unknown.
bool parse_backend(const std::string &name, BackendKind &out);

/// Options that affect code generation rather than the front end.
struct CodegenOptions {
    OptLevel opt = OptLevel::Basic;
    /// When false, integer arithmetic goes through the runtime's checked
    /// helpers. When true, it uses raw machine operations. PunPun specifies
    /// checked arithmetic, so this is off unless the user asks for it.
    bool unchecked_arithmetic = false;
    /// Emit `#line` directives (C backend) or `.loc` (native) so a debugger and
    /// the host compiler's own diagnostics point back at PunPun source.
    bool debug_info = false;
    /// Name used in the generated file's header comment.
    std::string source_name;
};

/// Common interface every code generator implements.
class Backend {
  public:
    virtual ~Backend() = default;

    /// Produces the backend's textual or binary output for the whole program.
    /// Returns false and reports diagnostics when the program uses something
    /// this backend cannot represent.
    virtual bool emit(const MirProgram &program, const CodegenOptions &options,
                      std::string &out) = 0;

    /// File extension for the emitted artifact, without the dot.
    virtual const char *artifact_extension() const = 0;
};

}  // namespace ppc

#endif
