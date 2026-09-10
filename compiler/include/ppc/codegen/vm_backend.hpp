#ifndef PPC_CODEGEN_VM_BACKEND_HPP
#define PPC_CODEGEN_VM_BACKEND_HPP

#include <unordered_map>
#include <vector>

#include "ppc/codegen/backend.hpp"
#include "ppc/codegen/bytecode.hpp"
#include "ppc/sema/type.hpp"

namespace ppc {

/// Compiles MIR to bytecode for the bundled VM.
///
/// This is the fastest path from source to running code: there is no text
/// generation, no host compiler, and no linker. The trade is runtime speed,
/// since every operation goes through the interpreter loop instead of becoming
/// a machine instruction.
class VmBackend : public Backend {
  public:
    VmBackend(TypeContext &types, DiagnosticEngine &diagnostics)
        : types_(types), diagnostics_(diagnostics) {}

    bool emit(const MirProgram &program, const CodegenOptions &options,
              std::string &out) override;
    const char *artifact_extension() const override { return "ppb"; }

    /// Compiles without serializing, for `ppc run` which interprets in-process.
    bool compile(const MirProgram &program, const CodegenOptions &options,
                 BytecodeProgram &out);

  private:
    void compile_function(const MirFunction &fn, std::size_t index);
    void compile_instruction(const MirFunction &fn, const MirInst &instruction);
    /// Chooses the opcode for a binary operation from its operand type.
    Op binary_opcode(BinaryOp op, TypeKind operand, bool unchecked) const;

    u32 intern_string(const std::string &text);

    /// Declaration index when `type` is a value struct that must be copied on
    /// assignment, or 0xFFFFFFFF when the value can be shared. Objects and
    /// enums share, since neither can be mutated in a way that reveals it.
    u32 copy_decl_for(const Type *type) const;
    /// Emits a deep copy of `source` and returns the slot holding the copy.
    /// When no copy is needed the source slot is returned unchanged.
    u32 copy_if_value_struct(const Type *type, u32 slot, const MirInst &at);
    void build_layouts();

    /// Scratch slots appended past the register range for copy temporaries.
    u32 scratch_slot();
    /// Frame slot for a register: locals occupy the low slots, so registers are
    /// offset past them.
    u32 slot_for_register(Reg id) const;
    u32 slot_for_local(u32 index) const { return index; }

    Instr &emit_instruction(Op op, const MirInst &source);

    TypeContext &types_;
    DiagnosticEngine &diagnostics_;
    CodegenOptions options_;
    BytecodeProgram *program_ = nullptr;
    const MirProgram *mir_ = nullptr;

    /// Per-function state.
    const MirFunction *function_ = nullptr;
    u32 local_count_ = 0;
    /// Frame size so far, grown by scratch_slot for copy temporaries.
    u32 frame_size_ = 0;
    /// Instruction index where each MIR block starts, for patching jumps.
    std::vector<u32> block_starts_;
    /// Jump sites to fix up once every block's address is known.
    std::vector<std::pair<u32, BlockId>> pending_jumps_;
    std::unordered_map<std::string, u32> string_index_;
};

}  // namespace ppc

#endif
