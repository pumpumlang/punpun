#ifndef PPC_MIR_BUILDER_HPP
#define PPC_MIR_BUILDER_HPP

#include <vector>

#include "ppc/hir/hir.hpp"
#include "ppc/mir/mir.hpp"
#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"

namespace ppc {

/// Lowers typed HIR into the MIR control-flow graph.
///
/// This is where structured control flow becomes blocks and branches: `if`,
/// `while`, `for`, `match`, and short-circuiting `and`/`or` all turn into
/// explicit edges. Locals stay as memory slots rather than being promoted to
/// registers here, because the C backend hands them to the host compiler as
/// ordinary C locals and the native backends promote them in a later pass.
class MirBuilder {
  public:
    MirBuilder(TypeContext &types, Arena &arena, DiagnosticEngine &diagnostics)
        : types_(types), arena_(arena), diagnostics_(diagnostics) {}

    MirProgram *build(const HirProgram &program);

  private:
    /// Break and continue targets for the innermost enclosing loop.
    struct LoopFrame {
        BlockId continue_target = kNoBlock;
        BlockId break_target = kNoBlock;
    };

    // -- block plumbing -----------------------------------------------------
    /// True when the current block already ended, so further emission would be
    /// dead code. Emission is silently dropped in that state.
    bool terminated() const { return current_ == kNoBlock; }
    void start_block(BlockId block) { current_ = block; }
    MirInst &emit(MirOp op, Span span);
    void emit_jump(BlockId target, Span span);
    void emit_branch(Reg condition, BlockId then_block, BlockId else_block, Span span);

    // -- lowering -----------------------------------------------------------
    void lower_function(const HirFunction &source, MirFunction &out);
    void lower_block(const std::vector<HirStmt *> &body);
    void lower_stmt(const HirStmt *statement);
    void lower_if(const HirStmt *statement);
    void lower_while(const HirStmt *statement);
    void lower_for(const HirStmt *statement);
    void flush_captures(Span span);

    Reg lower_expr(const HirExpr *expr);
    Reg lower_binary(const HirExpr *expr);
    Reg lower_short_circuit(const HirExpr *expr);
    Reg lower_match(const HirExpr *expr);
    Reg lower_call(const HirExpr *expr);

    /// Writes `value` into the place described by `place`. Value structs are
    /// updated with a read-modify-write chain; objects are written through.
    void lower_assign(const HirExpr *place, Reg value, Span span);

    // -- helpers ------------------------------------------------------------
    Reg const_int(i64 value, Span span);
    Reg const_bool(bool value, Span span);
    /// Adds a local slot that has no HIR counterpart, used for match results
    /// and short-circuit temporaries.
    u32 new_temp_local(const Type *type);
    /// Marks locals whose address is taken so the promotion pass leaves them
    /// in memory.
    void mark_address_taken(const HirExpr *expr);

    TypeContext &types_;
    Arena &arena_;
    DiagnosticEngine &diagnostics_;

    MirProgram *program_ = nullptr;
    MirFunction *function_ = nullptr;
    BlockId current_ = kNoBlock;
    std::vector<LoopFrame> loops_;
    /// Register holding the value being matched, for MatchSubject nodes.
    std::vector<Reg> match_subjects_;
};

}  // namespace ppc

#endif
