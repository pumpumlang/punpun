#include "ppc/mir/passes.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace ppc {

void PassStats::add(const PassStats &other) {
    folded_constants += other.folded_constants;
    propagated_copies += other.propagated_copies;
    removed_instructions += other.removed_instructions;
    removed_blocks += other.removed_blocks;
    simplified_branches += other.simplified_branches;
    merged_blocks += other.merged_blocks;
    promoted_locals += other.promoted_locals;
    iterations += other.iterations;
}

u32 PassStats::total() const {
    return folded_constants + propagated_copies + removed_instructions + removed_blocks +
           simplified_branches + merged_blocks + promoted_locals;
}

std::string PassStats::summary() const {
    std::ostringstream out;
    out << "constants folded:      " << folded_constants << "\n"
        << "copies propagated:     " << propagated_copies << "\n"
        << "instructions removed:  " << removed_instructions << "\n"
        << "branches simplified:   " << simplified_branches << "\n"
        << "blocks merged:         " << merged_blocks << "\n"
        << "blocks removed:        " << removed_blocks << "\n"
        << "optimizer iterations:  " << iterations << "\n";
    return out.str();
}

namespace {

/// Tracks which registers currently hold a known constant.
struct ConstantValue {
    enum class Kind { None, Int, Bool, Float } kind = Kind::None;
    i64 int_value = 0;
    double float_value = 0.0;
};

/// Checked signed arithmetic. Returns false when the operation would overflow,
/// which is the signal to leave the instruction alone for the runtime to trap.
bool checked_add(i64 a, i64 b, i64 &out) { return !__builtin_add_overflow(a, b, &out); }
bool checked_sub(i64 a, i64 b, i64 &out) { return !__builtin_sub_overflow(a, b, &out); }
bool checked_mul(i64 a, i64 b, i64 &out) { return !__builtin_mul_overflow(a, b, &out); }

/// Rewrites an instruction into a constant.
///
/// Clearing the operand registers matters as much as setting the value: dead
/// code elimination counts uses by scanning the operand fields, so a folded
/// instruction that still names its old inputs keeps them artificially alive.
void become_constant(MirInst &instruction, MirOp op, i64 value, const Type *type) {
    instruction.op = op;
    instruction.imm = value;
    instruction.type = type;
    instruction.a = kNoReg;
    instruction.b = kNoReg;
    instruction.c = kNoReg;
    instruction.args.clear();
}

/// Collects every register read by an instruction.
void collect_uses(const MirInst &instruction, std::vector<Reg> &out) {
    auto add = [&](Reg reg) {
        if (reg != kNoReg) out.push_back(reg);
    };
    add(instruction.a);
    add(instruction.b);
    add(instruction.c);
    for (Reg argument : instruction.args) add(argument);
}

}  // namespace

// ---------------------------------------------------------------------------
// Constant folding
// ---------------------------------------------------------------------------

u32 fold_constants(MirFunction &fn, const TypeContext &types) {
    u32 changed = 0;

    for (MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;

        // Constants are tracked per block. Without phi nodes there is no sound
        // way to carry a value across a join, so the map resets at each block.
        std::unordered_map<Reg, ConstantValue> known;

        for (MirInst &instruction : block.instructions) {
            switch (instruction.op) {
                case MirOp::ConstInt: {
                    ConstantValue value;
                    value.kind = ConstantValue::Kind::Int;
                    value.int_value = instruction.imm;
                    known[instruction.dest] = value;
                    continue;
                }
                case MirOp::ConstBool: {
                    ConstantValue value;
                    value.kind = ConstantValue::Kind::Bool;
                    value.int_value = instruction.imm;
                    known[instruction.dest] = value;
                    continue;
                }
                case MirOp::ConstFloat: {
                    ConstantValue value;
                    value.kind = ConstantValue::Kind::Float;
                    value.float_value = instruction.fimm;
                    known[instruction.dest] = value;
                    continue;
                }
                case MirOp::Unary: {
                    auto operand = known.find(instruction.a);
                    if (operand == known.end()) continue;
                    const ConstantValue &value = operand->second;

                    if (instruction.unary_op == UnaryOp::Not &&
                        value.kind == ConstantValue::Kind::Bool) {
                        become_constant(instruction, MirOp::ConstBool,
                                        value.int_value ? 0 : 1, types.bool_type());
                        ++changed;
                        ConstantValue folded;
                        folded.kind = ConstantValue::Kind::Bool;
                        folded.int_value = instruction.imm;
                        known[instruction.dest] = folded;
                        continue;
                    }
                    if (instruction.unary_op == UnaryOp::Negate &&
                        value.kind == ConstantValue::Kind::Int) {
                        i64 result = 0;
                        // Negating the smallest i64 overflows, so it is left for
                        // the runtime check.
                        if (!checked_sub(0, value.int_value, result)) continue;
                        become_constant(instruction, MirOp::ConstInt, result, types.int_type());
                        ++changed;
                        ConstantValue folded;
                        folded.kind = ConstantValue::Kind::Int;
                        folded.int_value = result;
                        known[instruction.dest] = folded;
                        continue;
                    }
                    if (instruction.unary_op == UnaryOp::BitNot &&
                        value.kind == ConstantValue::Kind::Int) {
                        become_constant(instruction, MirOp::ConstInt, ~value.int_value,
                                        types.int_type());
                        ++changed;
                        ConstantValue folded;
                        folded.kind = ConstantValue::Kind::Int;
                        folded.int_value = instruction.imm;
                        known[instruction.dest] = folded;
                    }
                    continue;
                }
                case MirOp::Binary: {
                    auto left = known.find(instruction.a);
                    auto right = known.find(instruction.b);
                    if (left == known.end() || right == known.end()) continue;

                    const ConstantValue &lhs = left->second;
                    const ConstantValue &rhs = right->second;
                    if (lhs.kind != rhs.kind) continue;

                    if (lhs.kind == ConstantValue::Kind::Int) {
                        i64 result = 0;
                        bool ok = true;
                        bool boolean = false;

                        switch (instruction.binary_op) {
                            case BinaryOp::Add: ok = checked_add(lhs.int_value, rhs.int_value, result); break;
                            case BinaryOp::Subtract: ok = checked_sub(lhs.int_value, rhs.int_value, result); break;
                            case BinaryOp::Multiply: ok = checked_mul(lhs.int_value, rhs.int_value, result); break;
                            case BinaryOp::Divide:
                                // Division by zero and INT64_MIN / -1 both trap
                                // at runtime, so neither is folded away.
                                if (rhs.int_value == 0) ok = false;
                                else if (lhs.int_value == INT64_MIN && rhs.int_value == -1) ok = false;
                                else result = lhs.int_value / rhs.int_value;
                                break;
                            case BinaryOp::Modulo:
                                if (rhs.int_value == 0) ok = false;
                                else if (lhs.int_value == INT64_MIN && rhs.int_value == -1) ok = false;
                                else result = lhs.int_value % rhs.int_value;
                                break;
                            case BinaryOp::BitAnd: result = lhs.int_value & rhs.int_value; break;
                            case BinaryOp::BitOr: result = lhs.int_value | rhs.int_value; break;
                            case BinaryOp::BitXor: result = lhs.int_value ^ rhs.int_value; break;
                            case BinaryOp::ShiftLeft:
                                if (rhs.int_value < 0 || rhs.int_value >= 64) ok = false;
                                else result = static_cast<i64>(static_cast<u64>(lhs.int_value)
                                                               << rhs.int_value);
                                break;
                            case BinaryOp::ShiftRight:
                                if (rhs.int_value < 0 || rhs.int_value >= 64) ok = false;
                                else result = lhs.int_value >> rhs.int_value;
                                break;
                            case BinaryOp::Equal: result = lhs.int_value == rhs.int_value; boolean = true; break;
                            case BinaryOp::NotEqual: result = lhs.int_value != rhs.int_value; boolean = true; break;
                            case BinaryOp::Less: result = lhs.int_value < rhs.int_value; boolean = true; break;
                            case BinaryOp::LessEqual: result = lhs.int_value <= rhs.int_value; boolean = true; break;
                            case BinaryOp::Greater: result = lhs.int_value > rhs.int_value; boolean = true; break;
                            case BinaryOp::GreaterEqual: result = lhs.int_value >= rhs.int_value; boolean = true; break;
                            default: ok = false;
                        }
                        if (!ok) continue;

                        become_constant(instruction,
                                        boolean ? MirOp::ConstBool : MirOp::ConstInt, result,
                                        boolean ? types.bool_type() : types.int_type());
                        ++changed;

                        ConstantValue folded;
                        folded.kind = boolean ? ConstantValue::Kind::Bool : ConstantValue::Kind::Int;
                        folded.int_value = result;
                        known[instruction.dest] = folded;
                        continue;
                    }

                    if (lhs.kind == ConstantValue::Kind::Bool) {
                        i64 result = 0;
                        if (instruction.binary_op == BinaryOp::Equal) {
                            result = (lhs.int_value != 0) == (rhs.int_value != 0);
                        } else if (instruction.binary_op == BinaryOp::NotEqual) {
                            result = (lhs.int_value != 0) != (rhs.int_value != 0);
                        } else {
                            continue;
                        }
                        become_constant(instruction, MirOp::ConstBool, result,
                                        types.bool_type());
                        ++changed;
                        ConstantValue folded;
                        folded.kind = ConstantValue::Kind::Bool;
                        folded.int_value = result;
                        known[instruction.dest] = folded;
                    }
                    continue;
                }
                default:
                    // Any other instruction may define a register whose value is
                    // not known; drop whatever was recorded for it.
                    if (instruction.dest != kNoReg) known.erase(instruction.dest);
                    continue;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Copy propagation
// ---------------------------------------------------------------------------

u32 propagate_copies(MirFunction &fn) {
    u32 changed = 0;

    for (MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;

        // slot -> register last stored into it, valid only until something
        // invalidates it.
        std::unordered_map<u32, Reg> stored;
        // register -> register it is a verbatim copy of.
        std::unordered_map<Reg, Reg> alias;

        auto resolve = [&](Reg reg) {
            auto it = alias.find(reg);
            return it == alias.end() ? reg : it->second;
        };

        for (MirInst &instruction : block.instructions) {
            // Rewrite reads first, so an instruction sees the earliest
            // equivalent register and later passes can delete the copies.
            auto rewrite = [&](Reg &reg) {
                if (reg == kNoReg) return;
                const Reg replacement = resolve(reg);
                if (replacement != reg) {
                    reg = replacement;
                    ++changed;
                }
            };
            rewrite(instruction.a);
            rewrite(instruction.b);
            rewrite(instruction.c);
            for (Reg &argument : instruction.args) rewrite(argument);

            switch (instruction.op) {
                case MirOp::StoreLocal:
                    stored[instruction.index] = instruction.a;
                    continue;
                case MirOp::LoadLocal: {
                    auto it = stored.find(instruction.index);
                    // A load right after a store of the same slot yields the
                    // value that was stored.
                    if (it != stored.end() && it->second != kNoReg) {
                        alias[instruction.dest] = it->second;
                    }
                    continue;
                }
                case MirOp::Call:
                case MirOp::CallBuiltin:
                case MirOp::Await:
                case MirOp::SetField:
                case MirOp::SetIndex:
                case MirOp::StoreDeref:
                case MirOp::Drop:
                    // A call can reach any local whose address escaped, and a
                    // write through a handle can alias anything. Be safe and
                    // forget everything.
                    stored.clear();
                    continue;
                default:
                    continue;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Branch simplification
// ---------------------------------------------------------------------------

u32 simplify_branches(MirFunction &fn) {
    u32 changed = 0;

    for (MirBlock &block : fn.blocks) {
        if (!block.reachable || block.instructions.empty()) continue;
        MirInst &last = block.instructions.back();

        if (last.op == MirOp::Branch && last.then_block == last.else_block) {
            last.op = MirOp::Jump;
            ++changed;
            continue;
        }
        if (last.op != MirOp::Branch) continue;

        // Look back for the definition of the condition inside this block.
        for (auto it = block.instructions.rbegin() + 1; it != block.instructions.rend(); ++it) {
            if (it->dest != last.a) continue;
            if (it->op == MirOp::ConstBool) {
                last.then_block = it->imm ? last.then_block : last.else_block;
                last.op = MirOp::Jump;
                last.a = kNoReg;
                ++changed;
            }
            break;
        }
    }

    if (changed) fn.compute_cfg();
    return changed;
}

// ---------------------------------------------------------------------------
// Dead code elimination
// ---------------------------------------------------------------------------

u32 eliminate_dead_code(MirFunction &fn) {
    u32 removed = 0;
    bool progress = true;

    // Iterated because deleting one instruction can make its operands dead.
    while (progress) {
        progress = false;

        std::vector<u32> use_count(fn.reg_types.size(), 0);
        for (const MirBlock &block : fn.blocks) {
            if (!block.reachable) continue;
            std::vector<Reg> uses;
            for (const MirInst &instruction : block.instructions) {
                uses.clear();
                collect_uses(instruction, uses);
                for (Reg reg : uses) {
                    if (reg < use_count.size()) ++use_count[reg];
                }
            }
        }

        for (MirBlock &block : fn.blocks) {
            if (!block.reachable) continue;
            const std::size_t before = block.instructions.size();

            block.instructions.erase(
                std::remove_if(block.instructions.begin(), block.instructions.end(),
                               [&](const MirInst &instruction) {
                                   if (instruction.has_side_effects()) return false;
                                   if (instruction.dest == kNoReg) return false;
                                   if (instruction.dest >= use_count.size()) return false;
                                   return use_count[instruction.dest] == 0;
                               }),
                block.instructions.end());

            if (block.instructions.size() != before) {
                removed += static_cast<u32>(before - block.instructions.size());
                progress = true;
            }
        }
    }

    // A store to a local that is never loaded anywhere is also dead, unless the
    // local is a parameter or had its address taken.
    std::vector<bool> loaded(fn.locals.size(), false);
    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        for (const MirInst &instruction : block.instructions) {
            if (instruction.op == MirOp::LoadLocal || instruction.op == MirOp::LocalAddr ||
                instruction.op == MirOp::Drop) {
                if (instruction.index < loaded.size()) loaded[instruction.index] = true;
            }
        }
    }
    for (MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        const std::size_t before = block.instructions.size();
        block.instructions.erase(
            std::remove_if(block.instructions.begin(), block.instructions.end(),
                           [&](const MirInst &instruction) {
                               if (instruction.op != MirOp::StoreLocal) return false;
                               if (instruction.index >= loaded.size()) return false;
                               if (loaded[instruction.index]) return false;
                               if (fn.locals[instruction.index].address_taken) return false;
                               return true;
                           }),
            block.instructions.end());
        removed += static_cast<u32>(before - block.instructions.size());
    }
    return removed;
}

// ---------------------------------------------------------------------------
// CFG cleanup
// ---------------------------------------------------------------------------

u32 remove_unreachable_blocks(MirFunction &fn) {
    fn.compute_cfg();

    u32 removed = 0;
    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) ++removed;
    }
    if (removed == 0) return 0;

    // Renumber: build an old-index -> new-index map, then rewrite every branch
    // target through it.
    std::vector<BlockId> mapping(fn.blocks.size(), kNoBlock);
    std::vector<MirBlock> survivors;
    survivors.reserve(fn.blocks.size() - removed);

    for (MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        mapping[block.id] = static_cast<BlockId>(survivors.size());
        survivors.push_back(std::move(block));
    }

    for (MirBlock &block : survivors) {
        block.id = mapping[block.id];
        for (MirInst &instruction : block.instructions) {
            if (instruction.op == MirOp::Jump || instruction.op == MirOp::Branch) {
                if (instruction.then_block < mapping.size()) {
                    instruction.then_block = mapping[instruction.then_block];
                }
                if (instruction.op == MirOp::Branch && instruction.else_block < mapping.size()) {
                    instruction.else_block = mapping[instruction.else_block];
                }
            }
        }
    }

    fn.entry_block = mapping[fn.entry_block] == kNoBlock ? 0 : mapping[fn.entry_block];
    fn.blocks = std::move(survivors);
    fn.compute_cfg();
    return removed;
}

u32 merge_blocks(MirFunction &fn) {
    fn.compute_cfg();
    u32 merged = 0;

    for (MirBlock &block : fn.blocks) {
        if (!block.reachable || block.instructions.empty()) continue;
        if (block.instructions.back().op != MirOp::Jump) continue;

        const BlockId target = block.instructions.back().then_block;
        if (target >= fn.blocks.size() || target == block.id) continue;

        MirBlock &successor = fn.blocks[target];
        // Only safe when this block is the successor's sole entry, otherwise the
        // other predecessors would lose their target.
        if (successor.predecessors.size() != 1) continue;
        if (successor.predecessors[0] != block.id) continue;
        if (target == fn.entry_block) continue;

        block.instructions.pop_back();  // drop the jump
        for (MirInst &instruction : successor.instructions) {
            block.instructions.push_back(std::move(instruction));
        }
        successor.instructions.clear();
        successor.reachable = false;
        ++merged;

        fn.compute_cfg();
    }

    if (merged) remove_unreachable_blocks(fn);
    return merged;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

PassStats optimize(MirFunction &fn, const TypeContext &types, OptLevel level) {
    PassStats stats;
    if (level == OptLevel::None) return stats;

    fn.compute_cfg();

    if (level == OptLevel::Basic) {
        // One cheap sweep: fold, propagate, then clean up. No fixed point, so
        // the cost stays proportional to the function size.
        stats.folded_constants += fold_constants(fn, types);
        stats.propagated_copies += propagate_copies(fn);
        stats.simplified_branches += simplify_branches(fn);
        stats.removed_instructions += eliminate_dead_code(fn);
        stats.removed_blocks += remove_unreachable_blocks(fn);
        stats.iterations = 1;
        return stats;
    }

    // Full: iterate until nothing changes, with a hard cap so a pathological
    // function cannot spin.
    constexpr u32 kMaxIterations = 8;
    for (u32 iteration = 0; iteration < kMaxIterations; ++iteration) {
        PassStats round;
        round.folded_constants = fold_constants(fn, types);
        round.propagated_copies = propagate_copies(fn);
        round.simplified_branches = simplify_branches(fn);
        round.removed_instructions = eliminate_dead_code(fn);
        round.removed_blocks = remove_unreachable_blocks(fn);
        round.merged_blocks = merge_blocks(fn);

        stats.add(round);
        ++stats.iterations;
        if (round.total() == 0) break;
    }
    return stats;
}

PassStats optimize(MirProgram &program, const TypeContext &types, OptLevel level) {
    PassStats stats;
    for (MirFunction *fn : program.functions) {
        if (fn->is_extern_native) continue;
        stats.add(optimize(*fn, types, level));
    }
    return stats;
}

}  // namespace ppc
