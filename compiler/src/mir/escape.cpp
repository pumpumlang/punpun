#include "ppc/mir/escape.hpp"

#include <algorithm>

namespace ppc {

namespace {

/// A small sorted set of allocation ids. Most registers hold at most one or two
/// allocations, so a sorted vector beats a hash set on both memory and speed at
/// this size, and it keeps iteration order deterministic — which matters,
/// because frame-slot assignment is derived from it and must be reproducible.
using ObjectSet = std::vector<Reg>;

bool insert_object(ObjectSet &set, Reg id) {
    auto position = std::lower_bound(set.begin(), set.end(), id);
    if (position != set.end() && *position == id) return false;
    set.insert(position, id);
    return true;
}

/// Adds every element of `from` to `into`. Returns true when `into` grew, which
/// is what drives the fixpoint loop.
bool merge_objects(ObjectSet &into, const ObjectSet &from) {
    bool changed = false;
    for (Reg id : from) changed |= insert_object(into, id);
    return changed;
}

/// True for aggregates that may live in the frame if nothing observes them
/// afterwards. Identity objects are excluded unconditionally: their semantics
/// are defined by reference, so giving one a stack home would let two handles
/// to "the same" object compare as distinct.
bool promotable_kind(const Type *type) {
    if (!type) return false;
    return type->kind == TypeKind::Struct || type->kind == TypeKind::Enum;
}

/// True when moving this value to a new home produces an independent copy.
///
/// The native and bytecode backends deep-copy a value struct at every transfer:
/// binding it to a local, storing it into a field, passing it as an argument,
/// or reading it out of a container. So the *original* block is never reachable
/// from the new home, and an edge that would otherwise let it escape does not.
///
/// This is the analysis's one backend precondition, and it is why the results
/// are only valid for backends that box aggregates and copy on transfer. The C
/// backend neither needs nor uses this analysis: C value semantics already give
/// it stack allocation and copy-on-assignment for free.
///
/// Enums are excluded deliberately. They are immutable once built, so the
/// backends share their blocks rather than copying, which means an enum really
/// can escape through an argument.
bool transfers_by_copy(const Type *type) {
    return type && type->kind == TypeKind::Struct;
}

}  // namespace

EscapeInfo analyze_escapes(const MirFunction &fn, const TypeContext &types) {
    EscapeInfo info;
    const std::size_t registers = fn.reg_types.size();
    info.stack_allocatable.assign(registers, false);
    info.frame_slot.assign(registers, 0);
    info.fresh_single_use.assign(registers, false);

    if (fn.is_extern_native || registers == 0) return info;

    // An allocation is identified by the register its MakeStruct/MakeEnum
    // defines, so no separate id space is needed.
    std::vector<bool> is_allocation(registers, false);
    std::vector<bool> escapes(registers, false);
    std::vector<ObjectSet> holds(registers);              // register -> allocations
    std::vector<ObjectSet> local_holds(fn.locals.size()); // local slot -> allocations
    // children[o] is everything reachable from allocation o by one field read.
    // Escape propagates along these edges: if a container escapes, so does
    // everything stored inside it.
    std::vector<ObjectSet> children(registers);

    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        for (const MirInst &in : block.instructions) {
            if (in.op != MirOp::MakeStruct && in.op != MirOp::MakeEnum) continue;
            if (in.dest == kNoReg || in.dest >= registers) continue;
            is_allocation[in.dest] = true;
            insert_object(holds[in.dest], in.dest);
            ++info.total_allocations;
            // An identity object is an allocation we count but never promote.
            if (!promotable_kind(in.type)) escapes[in.dest] = true;
        }
    }
    if (info.total_allocations == 0) return info;

    auto mark_escaped = [&](const ObjectSet &set) {
        for (Reg id : set) escapes[id] = true;
    };

    // Marks only the allocations that are handed over by reference. A value
    // struct is copied at every transfer, so the original does not escape.
    auto mark_escaped_by_reference = [&](const ObjectSet &set) {
        for (Reg id : set) {
            if (id < registers && transfers_by_copy(fn.reg_types[id])) continue;
            escapes[id] = true;
        }
    };

    // Fixpoint. The lattice only ever grows (sets gain members, escape flags go
    // false->true), so this terminates; the cap is a guard against a bug in the
    // transfer functions rather than an expected outcome.
    bool changed = true;
    int rounds = 0;
    constexpr int kMaxRounds = 64;
    while (changed && rounds++ < kMaxRounds) {
        changed = false;

        for (const MirBlock &block : fn.blocks) {
            if (!block.reachable) continue;
            for (const MirInst &in : block.instructions) {
                switch (in.op) {
                    case MirOp::LoadLocal:
                        if (in.dest < registers && in.index < local_holds.size()) {
                            changed |= merge_objects(holds[in.dest], local_holds[in.index]);
                        }
                        break;

                    case MirOp::StoreLocal:
                        // Binding a value struct to a local stores a copy, so
                        // the local does not hold the original allocation.
                        if (in.a < registers && in.index < local_holds.size() &&
                            !transfers_by_copy(fn.reg_types[in.a])) {
                            changed |= merge_objects(local_holds[in.index], holds[in.a]);
                        }
                        break;

                    case MirOp::LocalAddr:
                        // The address of the local is now a first-class value
                        // this analysis does not track, so everything it holds
                        // has to be assumed reachable from anywhere.
                        if (in.index < local_holds.size()) mark_escaped(local_holds[in.index]);
                        break;

                    case MirOp::MakeStruct:
                    case MirOp::MakeEnum:
                        // An initializer that is copied into the aggregate
                        // leaves the original unreachable from it; one that is
                        // shared becomes a child and inherits its escape state.
                        if (in.dest < registers) {
                            for (Reg argument : in.args) {
                                if (argument >= registers) continue;
                                if (transfers_by_copy(fn.reg_types[argument])) continue;
                                changed |= merge_objects(children[in.dest], holds[argument]);
                            }
                        }
                        break;

                    case MirOp::SetField:
                        // A copied value leaves the original unreachable from
                        // the container; a shared one becomes a child.
                        if (in.a < registers && in.b < registers &&
                            !transfers_by_copy(fn.reg_types[in.b])) {
                            for (Reg container : holds[in.a]) {
                                changed |= merge_objects(children[container], holds[in.b]);
                            }
                        }
                        break;

                    case MirOp::GetField:
                    case MirOp::EnumPayload:
                        // Reading a field yields something reachable from the
                        // container. Which field is not tracked, so the result
                        // conservatively holds every child.
                        if (in.dest < registers && in.a < registers) {
                            for (Reg container : holds[in.a]) {
                                changed |= merge_objects(holds[in.dest], children[container]);
                            }
                        }
                        break;

                    case MirOp::Return:
                        if (in.a != kNoReg && in.a < registers) mark_escaped(holds[in.a]);
                        break;

                    case MirOp::Call:
                    case MirOp::CallBuiltin:
                        // A callee may store an argument anywhere, so anything
                        // it receives by reference escapes. A value struct is
                        // copied at the call boundary, so the callee never sees
                        // the original block and that block does not escape.
                        for (Reg argument : in.args) {
                            if (argument < registers) mark_escaped_by_reference(holds[argument]);
                        }
                        break;

                    case MirOp::SetIndex:
                        if (in.c < registers) mark_escaped(holds[in.c]);
                        break;

                    case MirOp::StoreDeref:
                        if (in.b < registers) mark_escaped(holds[in.b]);
                        break;

                    case MirOp::Deref:
                        // The pointee is outside what this analysis models.
                        if (in.dest < registers && in.a < registers) mark_escaped(holds[in.a]);
                        break;

                    default:
                        break;
                }
            }
        }

        // Escape flows from a container to everything it holds.
        for (Reg container = 0; container < registers; ++container) {
            if (!escapes[container]) continue;
            for (Reg child : children[container]) {
                if (!escapes[child]) {
                    escapes[child] = true;
                    changed = true;
                }
            }
        }
    }

    // Count reads of every register, so a freshly built aggregate consumed
    // exactly once can be handed over rather than copied.
    {
        std::vector<u32> uses(registers, 0);
        for (const MirBlock &block : fn.blocks) {
            if (!block.reachable) continue;
            for (const MirInst &in : block.instructions) {
                auto count = [&](Reg id) {
                    if (id != kNoReg && id < registers) ++uses[id];
                };
                count(in.a);
                count(in.b);
                count(in.c);
                for (Reg argument : in.args) count(argument);
            }
        }
        for (Reg id = 0; id < registers; ++id) {
            if (!is_allocation[id]) continue;
            if (uses[id] != 1) continue;
            // Only value structs are copied on transfer, so only they have a
            // copy worth eliding.
            if (!transfers_by_copy(fn.reg_types[id])) continue;
            info.fresh_single_use[id] = true;
            ++info.elidable_copies;
        }
    }

    // Assign frame slots in register order so the layout is deterministic.
    for (Reg id = 0; id < registers; ++id) {
        if (!is_allocation[id] || escapes[id]) continue;

        std::size_t slots = 0;
        const Type *type = fn.reg_types[id];
        if (!type) continue;
        if (type->kind == TypeKind::Struct && type->decl < types.struct_count()) {
            slots = types.struct_at(type->decl).fields.size();
        } else if (type->kind == TypeKind::Enum && type->decl < types.enum_count()) {
            // One slot for the discriminant, then the widest variant payload.
            std::size_t widest = 0;
            for (const VariantInfo &variant : types.enum_at(type->decl).variants) {
                widest = std::max(widest, variant.payload.size());
            }
            slots = 1 + widest;
        } else {
            continue;
        }
        if (slots == 0) slots = 1;  // C forbids a zero-size object; so do we

        info.stack_allocatable[id] = true;
        info.frame_slot[id] = info.frame_slots_needed;
        info.frame_slots_needed += static_cast<u32>(slots);
        ++info.promoted_allocations;
    }

    return info;
}

// ---------------------------------------------------------------------------
// Parameter usage
// ---------------------------------------------------------------------------

ParameterUsage analyze_parameters(const MirFunction &fn) {
    ParameterUsage usage;
    usage.read_only.assign(fn.param_count, true);
    if (fn.is_extern_native) {
        // No body to inspect, so nothing can be proven.
        usage.read_only.assign(fn.param_count, false);
        return usage;
    }

    // Registers that currently hold a parameter's value, so a store through one
    // can be attributed back to the parameter it came from.
    std::vector<int> from_parameter(fn.reg_types.size(), -1);

    auto mark_mutable = [&](Reg id) {
        if (id < from_parameter.size() && from_parameter[id] >= 0) {
            usage.read_only[from_parameter[id]] = false;
        }
    };

    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        for (const MirInst &in : block.instructions) {
            switch (in.op) {
                case MirOp::LoadLocal:
                    if (in.dest < from_parameter.size() && in.index < fn.param_count) {
                        from_parameter[in.dest] = static_cast<int>(in.index);
                    }
                    break;

                case MirOp::StoreLocal:
                    // Rebinding the parameter slot itself does not mutate the
                    // caller's value, but any allocation it held is no longer
                    // tracked, so stop attributing that register.
                    if (in.index < fn.param_count) {
                        // Writing a new value into the parameter slot is local
                        // to the callee and safe.
                    }
                    break;

                case MirOp::SetField:
                    // Writing into the parameter's block is visible to the
                    // caller if the block was shared, so the copy is required.
                    mark_mutable(in.a);
                    break;

                case MirOp::SetIndex:
                case MirOp::StoreDeref:
                    mark_mutable(in.a);
                    break;

                case MirOp::LocalAddr:
                    if (in.index < fn.param_count) usage.read_only[in.index] = false;
                    break;

                case MirOp::Return:
                    // Returning the parameter lets it outlive the call.
                    if (in.a != kNoReg) mark_mutable(in.a);
                    break;

                case MirOp::Call:
                case MirOp::CallBuiltin:
                    // Passing it onward hands it to code this analysis has not
                    // examined.
                    for (Reg argument : in.args) mark_mutable(argument);
                    break;

                case MirOp::MakeStruct:
                case MirOp::MakeEnum:
                    // Storing the parameter into a new aggregate could let it
                    // outlive the call if that aggregate escapes.
                    for (Reg argument : in.args) mark_mutable(argument);
                    break;

                default:
                    break;
            }
        }
    }
    return usage;
}

}  // namespace ppc
