#ifndef PPC_CODEGEN_C_BACKEND_HPP
#define PPC_CODEGEN_C_BACKEND_HPP

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ppc/codegen/backend.hpp"
#include "ppc/sema/type.hpp"

namespace ppc {

/// Emits a single self-contained C11 translation unit.
///
/// This is the highest-performance path: the host C compiler gets a whole
/// program in one file and can inline, vectorize, and register-allocate across
/// all of it. MIR locals become ordinary C locals, so the host compiler's own
/// SSA construction does the promotion work that ppc's optimizer deliberately
/// leaves undone.
class CBackend : public Backend {
  public:
    CBackend(TypeContext &types, DiagnosticEngine &diagnostics)
        : types_(types), diagnostics_(diagnostics) {}

    bool emit(const MirProgram &program, const CodegenOptions &options,
              std::string &out) override;
    const char *artifact_extension() const override { return "c"; }

  private:
    // -- naming -------------------------------------------------------------
    /// C spelling for a PunPun type, including the pointer for handle types.
    std::string type_name(const Type *type);
    /// Tag name for an aggregate, unique across the program.
    std::string aggregate_name(const Type *type);
    /// Sanitized, unique C identifier for a function.
    std::string function_name(std::size_t index) const;
    std::string handle_cast(const Type *want, const Type *have);
    static std::string sanitize(const std::string &name);
    static std::string quote(const std::string &text);

    // -- declarations -------------------------------------------------------
    /// Emits struct, object, and enum definitions in dependency order, since a
    /// value-embedded aggregate must be complete before it is used.
    void emit_aggregates();
    void emit_aggregate(const Type *type, std::vector<const Type *> &pending);
    void collect_aggregates(const MirProgram &program);

    void emit_prototypes(const MirProgram &program);
    void emit_task_trampolines(const MirProgram &program);
    void emit_function(const MirFunction &fn, std::size_t index);
    void emit_instruction(const MirFunction &fn, const MirInst &instruction);
    void emit_builtin(const MirFunction &fn, const MirInst &instruction);
    void emit_binary(const MirFunction &fn, const MirInst &instruction);
    void emit_entry(const MirProgram &program);

    // -- helpers ------------------------------------------------------------
    std::string reg(Reg id) const { return "r" + std::to_string(id); }
    std::string local(u32 slot) const { return "v" + std::to_string(slot); }
    /// `->` for handle types, `.` for values.
    static const char *access(const Type *type);
    const Type *reg_type(const MirFunction &fn, Reg id) const;
    /// Awaits produce different runtime calls depending on the payload type.
    std::string await_call(const Type *result, const std::string &task) const;

    TypeContext &types_;
    DiagnosticEngine &diagnostics_;
    CodegenOptions options_;
    std::ostringstream out_;

    /// Aggregate types reachable from the program, in discovery order.
    std::vector<const Type *> aggregates_;
    std::unordered_map<const Type *, std::string> aggregate_names_;
    std::unordered_map<const Type *, int> emit_state_;  // 0 none, 1 active, 2 done
    const MirProgram *program_ = nullptr;
    /// Serial number for generated temporaries. A destination register is not
    /// usable as an identity here, because a void call has none.
    u32 temporary_id_ = 0;
};

}  // namespace ppc

#endif
