#ifndef PPC_MIR_ESCAPE_HPP
#define PPC_MIR_ESCAPE_HPP

#include <vector>

#include "ppc/mir/mir.hpp"
#include "ppc/sema/type.hpp"

namespace ppc {

/// Which aggregates a function creates that cannot outlive it.
///
/// The native and bytecode backends box every aggregate into a heap block,
/// because that is the only representation that is correct in general. It is
/// badly pessimistic for the common case: a struct built, read, and discarded
/// inside one function never needs to be on the heap at all. Measured on
/// tests/benchmarks/aggregates.pp, that boxing is what makes the native backend
/// ~56x slower than the C backend, which gets stack allocation for free from C
/// value semantics.
///
/// This analysis finds the allocations that provably do not escape so a backend
/// can put them in the frame instead.
///
/// CORRECTNESS CONTRACT
///
/// An allocation may be stack-allocated only when no reference to it can be
/// observed after the function returns. The analysis is a may-escape analysis:
/// anything it cannot prove local is treated as escaping. Specifically an
/// allocation escapes when it is
///
///   * returned,
///   * passed to any call (the callee may retain it),
///   * written through a pointer or into a list element,
///   * reachable from another allocation that escapes, or
///   * held in a local whose address is taken.
///
/// Identity objects (`object`, TypeKind::Object) are never promoted regardless
/// of what the analysis finds. Their semantics are defined by reference, and a
/// stack copy would give two handles distinct identity. Value structs and enums
/// have no such requirement.
struct EscapeInfo {
    /// Indexed by register. True when that register is defined by a MakeStruct
    /// or MakeEnum whose allocation is provably function-local.
    std::vector<bool> stack_allocatable;

    /// Frame offset assigned to each promoted allocation, in slots from the
    /// start of the aggregate storage area. Only meaningful where
    /// stack_allocatable is true.
    std::vector<u32> frame_slot;
    /// Slots of aggregate storage the frame must reserve.
    u32 frame_slots_needed = 0;

    /// Indexed by register. True when the register is defined by a MakeStruct
    /// or MakeEnum and read exactly once.
    ///
    /// Such a value is freshly built and unaliased, so the single consumer can
    /// take ownership of the block instead of deep-copying it. This is ordinary
    /// copy elision, and it matters here because the backends otherwise copy a
    /// value struct at *every* transfer, which makes `let p = Point(1, 2)` cost
    /// one allocation for the literal and a second for the copy into `p`.
    std::vector<bool> fresh_single_use;

    // Reported through optimizer statistics and optimization remarks.
    u32 total_allocations = 0;
    u32 promoted_allocations = 0;
    u32 elidable_copies = 0;

    bool can_take_ownership(Reg id) const {
        return id < fresh_single_use.size() && fresh_single_use[id];
    }

    bool promoted(Reg id) const {
        return id < stack_allocatable.size() && stack_allocatable[id];
    }
};

/// Runs the analysis. Pure: it reads the function and allocates nothing in it.
EscapeInfo analyze_escapes(const MirFunction &fn, const TypeContext &types);

/// Whether a parameter is only read.
///
/// A value struct passed by value is defensively deep-copied at the call site,
/// because the callee is free to mutate its own copy. When the callee provably
/// never writes to the parameter and never lets it outlive the call, that copy
/// is dead work and the caller can pass the original block.
///
/// CORRECTNESS CONTRACT
///
/// A parameter is read-only when, within the callee, it is never the target of
/// a field or element store, never returned, never passed onward to another
/// call, and never has its address taken. Anything else marks it mutable.
struct ParameterUsage {
    /// Indexed by parameter position. True when the callee only reads it.
    std::vector<bool> read_only;
};

ParameterUsage analyze_parameters(const MirFunction &fn);

}  // namespace ppc

#endif
