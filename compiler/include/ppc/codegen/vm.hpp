#ifndef PPC_CODEGEN_VM_HPP
#define PPC_CODEGEN_VM_HPP

#include <string>
#include <vector>

#include "ppc/codegen/bytecode.hpp"

namespace ppc {

/// One frame slot.
///
/// Slots are untagged: MIR is fully typed, so the compiler already knows which
/// interpretation each slot holds and picks the matching opcode. That removes a
/// tag check from every operation, which is most of what makes this interpreter
/// worth using at all.
union Slot {
    i64 integer;
    double real;
    void *pointer;
    const char *text;

    Slot() : integer(0) {}
};

/// Executes a compiled bytecode program.
///
/// The VM links against the same runtime the native backends use, so builtins,
/// panics, and memory behave identically no matter which backend produced the
/// program. A program's observable output should not depend on the backend.
class Vm {
  public:
    Vm() = default;

    /// Runs the program's entry point. Returns the process exit status.
    /// `argc`/`argv` are handed to the runtime so `arg`/`arg_count` work.
    int run(const BytecodeProgram &program, int argc, char **argv);

    /// Entry point for a worker thread: runs one function on a fresh
    /// interpreter over the same immutable program.
    ///
    /// Each task gets its own Vm because the interpreter's state is a stack and
    /// a depth counter, neither of which can be shared. The BytecodeProgram is
    /// read-only after compilation, so every worker can point at the same one.
    Slot run_task(const BytecodeProgram &program, u32 function, const Slot *arguments,
                  u32 argument_count);

    const std::string &error() const { return error_; }

  private:
    /// Executes one function with `arguments` already evaluated. Returns the
    /// function's result, or a zero slot for a void function.
    Slot invoke(u32 function_index, const Slot *arguments, u32 argument_count);
    /// Deep-copies a boxed value struct, following nested value fields.
    void *copy_struct(void *source, u32 decl);
    /// `formatter` selects the print family's output form; it is ignored by
    /// every other builtin.
    Slot call_builtin(u32 builtin, const Slot *arguments, u32 argument_count, u32 formatter);

    [[noreturn]] void fail(const std::string &message);

    const BytecodeProgram *program_ = nullptr;
    /// One contiguous stack for every frame, grown on demand. Frames are
    /// carved out of it rather than heap-allocated per call.
    std::vector<Slot> stack_;
    std::size_t stack_top_ = 0;
    /// Guards against runaway recursion producing a native stack overflow.
    u32 depth_ = 0;
    static constexpr u32 kMaxDepth = 8192;
    std::string error_;
};

}  // namespace ppc

#endif
