#ifndef PPC_MIR_PASSES_HPP
#define PPC_MIR_PASSES_HPP

#include <string>
#include <vector>

#include "ppc/mir/mir.hpp"

namespace ppc {

/// How hard the optimizer works. This maps directly onto the `-O` flag and is
/// the main lever on the compile-time/run-time trade-off.
enum class OptLevel {
    /// No transforms at all. Fastest possible compile; MIR reaches the backend
    /// exactly as the builder emitted it.
    None = 0,
    /// Cheap, single-sweep local cleanups. Roughly free at compile time and
    /// removes the obvious waste the builder emits by construction.
    Basic = 1,
    /// The full pipeline, iterated to a fixed point.
    Full = 2,
};

/// Counts of what each pass changed, reported by `--stats`.
struct PassStats {
    u32 folded_constants = 0;
    u32 propagated_copies = 0;
    u32 removed_instructions = 0;
    u32 removed_blocks = 0;
    u32 simplified_branches = 0;
    u32 merged_blocks = 0;
    u32 promoted_locals = 0;
    u32 iterations = 0;

    void add(const PassStats &other);
    std::string summary() const;
    u32 total() const;
};

/// Evaluates instructions whose operands are all compile-time constants.
///
/// Integer arithmetic in PunPun is checked, so an operation that would overflow
/// is deliberately left alone: folding it would turn a runtime trap into a
/// silently wrong constant.
u32 fold_constants(MirFunction &fn, const TypeContext &types);

/// Forwards a value stored to a local into later loads of that local, within a
/// single block and only while no intervening write can have changed it.
u32 propagate_copies(MirFunction &fn);

/// Replaces a branch on a constant condition with an unconditional jump.
u32 simplify_branches(MirFunction &fn);

/// Deletes instructions whose result is never read and which cannot have side
/// effects.
u32 eliminate_dead_code(MirFunction &fn);

/// Deletes blocks the entry cannot reach and renumbers the survivors.
u32 remove_unreachable_blocks(MirFunction &fn);

/// Merges a block into its only predecessor when that predecessor's only
/// successor is this block.
u32 merge_blocks(MirFunction &fn);

/// Runs the pipeline for `level` and returns what it changed.
PassStats optimize(MirFunction &fn, const TypeContext &types, OptLevel level);
PassStats optimize(MirProgram &program, const TypeContext &types, OptLevel level);

}  // namespace ppc

#endif
