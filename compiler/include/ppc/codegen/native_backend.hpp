#ifndef PPC_CODEGEN_NATIVE_BACKEND_HPP
#define PPC_CODEGEN_NATIVE_BACKEND_HPP

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ppc/codegen/backend.hpp"
#include "ppc/mir/escape.hpp"
#include "ppc/sema/type.hpp"

namespace ppc {

/// Emits x86-64 System V assembly directly, skipping the host C compiler.
///
/// This sits between the other two backends on the compile-time / run-time
/// curve: there is no C parsing or host optimization pass to pay for. Locals
/// and MIR values retain deterministic spill homes, while a CFG-aware linear-
/// scan allocator keeps eligible scalar values in callee-saved registers across
/// operations and calls. System V register and stack arguments are both emitted
/// directly, including the wrappers used for native async tasks.
///
/// Aggregates are boxed on the heap exactly as in the bytecode backend, so a
/// `struct` gets an explicit deep copy wherever the language specifies value
/// semantics.
class NativeBackend : public Backend {
  public:
    NativeBackend(TypeContext &types, DiagnosticEngine &diagnostics)
        : types_(types), diagnostics_(diagnostics) {}

    bool emit(const MirProgram &program, const CodegenOptions &options,
              std::string &out) override;
    const char *artifact_extension() const override { return "s"; }

  private:
    // -- layout -------------------------------------------------------------
    /// Byte offset from %rbp for a MIR register's spill home.  Even registers
    /// selected by the native allocator retain a home so debugging, aggregate
    /// materialization, and unusual instructions can address them uniformly.
    int register_offset(Reg id) const;
    /// Assigns scalar MIR registers to callee-saved x86-64 registers with a
    /// conservative linear-scan allocator.  Values still get spill homes, but
    /// hot arithmetic can consume the allocated register directly.
    void allocate_registers(const MirFunction &fn);
    const char *allocated_register(Reg id) const;
    void load_value(const std::string &target, Reg id);
    void store_value(const std::string &source, Reg id);
    /// Hidden slot holding the closure object for the current indirect call.
    int closure_offset() const;
    /// Byte offset from %rbp for a local's slot.
    int local_offset(u32 index) const;
    /// Offset of a scratch slot used for a copy temporary.
    int scratch_offset(u32 index) const;
    /// Offset of frame storage for an aggregate the escape analysis promoted.
    int aggregate_offset(u32 slot) const;

    // -- emission -----------------------------------------------------------
    void emit_function(const MirFunction &fn, std::size_t index);
    void emit_instruction(const MirFunction &fn, const MirInst &instruction);
    void emit_binary(const MirFunction &fn, const MirInst &instruction);
    void emit_call(const MirFunction &fn, const MirInst &instruction);
    void emit_builtin(const MirFunction &fn, const MirInst &instruction);
    /// Per-type deep copy helpers, one per value struct that needs one.
    void emit_copy_helpers();
    void emit_entry(const MirProgram &program);
    void emit_string_pool();
    void emit_function_table(const MirProgram &program);
    /// Per-async-function worker trampolines and spawn wrappers.
    void emit_task_trampolines(const MirProgram &program);

    /// Loads a value into a GP register, or a double into an SSE register.
    void load(const std::string &reg, int offset);
    void store(const std::string &reg, int offset);
    void load_sse(const std::string &reg, int offset);
    void store_sse(const std::string &reg, int offset);

    /// Emits a checked arithmetic sequence: the operation followed by a
    /// conditional jump to a shared trap stub.
    void checked_op(const char *mnemonic, const char *trap_label);

    // -- naming -------------------------------------------------------------
    std::string function_label(std::size_t index) const;
    u32 intern_string(const std::string &text);
    static std::string sanitize(const std::string &name);
    /// Label for a basic block inside the function currently being emitted.
    std::string block_label(BlockId block) const;
    std::string epilogue_label() const;

    // -- aggregates ---------------------------------------------------------
    /// Declaration index when the type is a value struct needing a copy.
    u32 copy_decl_for(const Type *type) const;
    /// Emits a copy of the value in `source_offset` into a fresh scratch slot,
    /// returning that slot's offset. Returns `source_offset` when no copy is
    /// required.
    /// `source` is the register the value came from, or kNoReg when it is not
    /// a plain register read. It is what allows the copy to be elided.
    int copy_if_value_struct(const Type *type, int source_offset, Reg source = kNoReg);

    TypeContext &types_;
    DiagnosticEngine &diagnostics_;
    CodegenOptions options_;
    std::ostringstream out_;
    const MirProgram *program_ = nullptr;

    /// Which of this function's allocations the escape analysis proved local.
    /// Recomputed per function; empty means every allocation goes to the heap.
    EscapeInfo escapes_;

    /// Per-function frame accounting.
    const MirFunction *function_ = nullptr;
    /// Index of the function being emitted, used to build unique local labels.
    std::size_t function_index_ = 0;
    int frame_bytes_ = 0;
    u32 scratch_used_ = 0;
    u32 scratch_high_water_ = 0;
    /// -1 means spilled; otherwise indexes the small callee-saved register pool.
    std::vector<int> register_assignment_;
    std::vector<int> used_register_slots_;
    /// Labels for the traps this function needs, deduplicated.
    std::vector<std::string> traps_;

    /// Totals across the whole program, reported through --stats.
    u32 total_allocations_ = 0;
    u32 promoted_allocations_ = 0;
    u32 elided_copies_ = 0;

    std::vector<std::string> strings_;
    std::unordered_map<std::string, u32> string_index_;
    /// Value-struct declarations that need a copy helper emitted.
    std::vector<u32> copy_helpers_;
};

}  // namespace ppc

#endif
