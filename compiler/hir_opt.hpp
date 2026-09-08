#ifndef PUNPUN_HIR_OPT_HPP
#define PUNPUN_HIR_OPT_HPP

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hir.hpp"

namespace pphir {

struct ConstantValue {
    Type type;
    std::string text;
};

class Pass {
  public:
    virtual ~Pass() = default;
    virtual const char *name() const = 0;
    virtual bool run(Program &program) = 0;
};

class ConstantFoldPass final : public Pass {
  public:
    const char *name() const override { return "constant-fold"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) changed |= fold(function);
        return changed;
    }

  private:
    static bool add_i64(int64_t left, int64_t right, int64_t &out) {
        if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
            (right < 0 && left < std::numeric_limits<int64_t>::min() - right)) return false;
        out = left + right;
        return true;
    }
    static bool sub_i64(int64_t left, int64_t right, int64_t &out) {
        if ((right < 0 && left > std::numeric_limits<int64_t>::max() + right) ||
            (right > 0 && left < std::numeric_limits<int64_t>::min() + right)) return false;
        out = left - right;
        return true;
    }
    static std::optional<int64_t> integer(const ConstantValue &value) {
        if (value.type != Type::Int) return std::nullopt;
        try { return std::stoll(value.text); }
        catch (...) { return std::nullopt; }
    }
    static std::optional<bool> boolean(const ConstantValue &value) {
        if (value.type != Type::Bool) return std::nullopt;
        if (value.text == "yes") return true;
        if (value.text == "no") return false;
        return std::nullopt;
    }
    static ConstantValue boolean_value(bool value) { return {Type::Bool, value ? "yes" : "no"}; }

    static std::optional<ConstantValue> unary(const Instruction &instruction,
                                              const ConstantValue &operand) {
        if (instruction.detail == "not") {
            if (auto value = boolean(operand)) return boolean_value(!*value);
        }
        if (instruction.detail == "-") {
            if (auto value = integer(operand)) {
                if (*value != std::numeric_limits<int64_t>::min())
                    return ConstantValue{Type::Int, std::to_string(-*value)};
            }
        }
        return std::nullopt;
    }

    static std::optional<ConstantValue> binary(const Instruction &instruction,
                                               const ConstantValue &left,
                                               const ConstantValue &right) {
        const std::string &op = instruction.detail;
        if (auto a = integer(left)) {
            const auto b = integer(right);
            if (!b) return std::nullopt;
            int64_t value = 0;
            if (op == "+" && add_i64(*a, *b, value)) return ConstantValue{Type::Int, std::to_string(value)};
            if (op == "-" && sub_i64(*a, *b, value)) return ConstantValue{Type::Int, std::to_string(value)};
            if (op == "/" && *b != 0 && !(*a == std::numeric_limits<int64_t>::min() && *b == -1))
                return ConstantValue{Type::Int, std::to_string(*a / *b)};
            if (op == "%" && *b != 0 && !(*a == std::numeric_limits<int64_t>::min() && *b == -1))
                return ConstantValue{Type::Int, std::to_string(*a % *b)};
            if (op == "==") return boolean_value(*a == *b);
            if (op == "!=") return boolean_value(*a != *b);
            if (op == "<") return boolean_value(*a < *b);
            if (op == "<=") return boolean_value(*a <= *b);
            if (op == ">") return boolean_value(*a > *b);
            if (op == ">=") return boolean_value(*a >= *b);
        }
        if (auto a = boolean(left)) {
            const auto b = boolean(right);
            if (!b) return std::nullopt;
            if (op == "and") return boolean_value(*a && *b);
            if (op == "or") return boolean_value(*a || *b);
            if (op == "==") return boolean_value(*a == *b);
            if (op == "!=") return boolean_value(*a != *b);
        }
        if (left.type == Type::Str && right.type == Type::Str) {
            if (op == "+") return ConstantValue{Type::Str, left.text + right.text};
            if (op == "==") return boolean_value(left.text == right.text);
            if (op == "!=") return boolean_value(left.text != right.text);
        }
        return std::nullopt;
    }

    static bool fold(Function &function) {
        std::unordered_map<ValueId, ConstantValue> constants;
        for (const Block &block : function.blocks)
            for (const Instruction &instruction : block.instructions)
                if (instruction.op == Op::Constant && instruction.result != NoValue)
                    constants[instruction.result] = {instruction.type, instruction.detail};

        bool changed_any = false;
        bool changed_round = true;
        while (changed_round) {
            changed_round = false;
            for (Block &block : function.blocks) {
                for (Instruction &instruction : block.instructions) {
                    if (instruction.result == NoValue || instruction.op == Op::Constant) continue;
                    std::optional<ConstantValue> folded;
                    if (instruction.op == Op::Unary && instruction.operands.size() == 1) {
                        auto operand = constants.find(instruction.operands[0]);
                        if (operand != constants.end()) folded = unary(instruction, operand->second);
                    } else if (instruction.op == Op::Binary && instruction.operands.size() == 2) {
                        auto left = constants.find(instruction.operands[0]);
                        auto right = constants.find(instruction.operands[1]);
                        if (left != constants.end() && right != constants.end())
                            folded = binary(instruction, left->second, right->second);
                    }
                    if (!folded) continue;
                    instruction.op = Op::Constant;
                    instruction.type = folded->type;
                    instruction.detail = folded->text;
                    instruction.operands.clear();
                    constants[instruction.result] = *folded;
                    changed_round = changed_any = true;
                }
            }
        }
        return changed_any;
    }
};

class BranchSimplifyPass final : public Pass {
  public:
    const char *name() const override { return "branch-simplify"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            std::unordered_map<ValueId, bool> booleans;
            for (const Block &block : function.blocks)
                for (const Instruction &instruction : block.instructions)
                    if (instruction.op == Op::Constant && instruction.type == Type::Bool &&
                        instruction.result != NoValue && (instruction.detail == "yes" || instruction.detail == "no"))
                        booleans[instruction.result] = instruction.detail == "yes";
            for (Block &block : function.blocks) {
                Terminator &term = block.terminator;
                if (term.kind != Terminator::Kind::Branch) continue;
                auto value = booleans.find(term.value);
                if (value == booleans.end()) continue;
                const BlockId target = value->second ? term.first : term.second;
                term = {Terminator::Kind::Jump, NoValue, target, NoBlock, term.token};
                changed = true;
            }
        }
        return changed;
    }
};

class DeadBlockEliminationPass final : public Pass {
  public:
    const char *name() const override { return "dead-block-elimination"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) changed |= eliminate(function);
        return changed;
    }

  private:
    static bool eliminate(Function &function) {
        if (function.blocks.empty()) return false;
        std::vector<bool> reachable(function.blocks.size(), false);
        std::queue<BlockId> work;
        reachable[0] = true;
        work.push(0);
        while (!work.empty()) {
            const BlockId id = work.front(); work.pop();
            const Terminator &term = function.blocks[id].terminator;
            auto visit = [&](BlockId target) {
                if (target != NoBlock && target < reachable.size() && !reachable[target]) {
                    reachable[target] = true;
                    work.push(target);
                }
            };
            if (term.kind == Terminator::Kind::Jump) visit(term.first);
            else if (term.kind == Terminator::Kind::Branch) { visit(term.first); visit(term.second); }
        }
        if (std::all_of(reachable.begin(), reachable.end(), [](bool value) { return value; })) return false;

        std::vector<BlockId> remap(function.blocks.size(), NoBlock);
        std::vector<Block> kept;
        kept.reserve(function.blocks.size());
        for (BlockId old = 0; old < function.blocks.size(); ++old) {
            if (!reachable[old]) continue;
            remap[old] = kept.size();
            Block block = std::move(function.blocks[old]);
            block.id = kept.size();
            kept.push_back(std::move(block));
        }
        for (Block &block : kept) {
            Terminator &term = block.terminator;
            if (term.kind == Terminator::Kind::Jump) term.first = remap.at(term.first);
            else if (term.kind == Terminator::Kind::Branch) {
                term.first = remap.at(term.first);
                term.second = remap.at(term.second);
            }
        }
        function.blocks = std::move(kept);
        return true;
    }
};

// Forward the most recent stored SSA value into later loads in the same block.
// Calls and address-taking form conservative memory barriers. This converts
// the stack-shaped frontend HIR into a more useful SSA value stream before GVN.
class LocalLoadForwardingPass final : public Pass {
  public:
    const char *name() const override { return "local-load-forwarding"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) changed |= forward(function);
        return changed;
    }

  private:
    static ValueId canonical(ValueId value, const std::unordered_map<ValueId, ValueId> &redirect) {
        auto found = redirect.find(value);
        while (found != redirect.end() && found->second != value) {
            value = found->second;
            found = redirect.find(value);
        }
        return value;
    }

    static std::string addressed_name(const Instruction &instruction) {
        if (instruction.op != Op::AddressOf) return {};
        const std::size_t space = instruction.detail.find(' ');
        if (space == std::string::npos || instruction.detail.find('.', space) != std::string::npos) return {};
        return instruction.detail.substr(space + 1);
    }

    static bool forward(Function &function) {
        bool changed = false;
        std::unordered_map<ValueId, ValueId> redirect;
        for (Block &block : function.blocks) {
            std::unordered_map<std::string, ValueId> current;
            std::vector<Instruction> kept;
            kept.reserve(block.instructions.size());
            for (Instruction instruction : block.instructions) {
                for (ValueId &operand : instruction.operands) operand = canonical(operand, redirect);
                if (instruction.op == Op::Store && instruction.operands.size() == 1) {
                    current[instruction.detail] = instruction.operands[0];
                } else if (instruction.op == Op::Load && instruction.result != NoValue) {
                    auto found = current.find(instruction.detail);
                    if (found != current.end()) {
                        redirect[instruction.result] = found->second;
                        changed = true;
                        continue;
                    }
                } else if (instruction.op == Op::AddressOf) {
                    const std::string name = addressed_name(instruction);
                    if (!name.empty()) current.erase(name);
                } else if (instruction.op == Op::Call || instruction.op == Op::Await ||
                           instruction.op == Op::StoreIndirect || instruction.op == Op::StoreMember ||
                           instruction.op == Op::StoreIndex) {
                    current.clear();
                }
                kept.push_back(std::move(instruction));
            }
            block.instructions = std::move(kept);
            if (block.terminator.value != NoValue)
                block.terminator.value = canonical(block.terminator.value, redirect);
        }
        return changed;
    }
};

// Local value numbering over each basic block. HIR value ids are already SSA
// names; this pass removes duplicate pure computations and redirects every use
// to the first dominating value. Keeping the pass block-local makes dominance
// explicit and avoids silently assuming that a value computed on one branch is
// available on another.
class LocalValueNumberingPass final : public Pass {
  public:
    const char *name() const override { return "local-gvn-cse"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) changed |= number(function);
        return changed;
    }

  private:
    static bool eligible(Op op) {
        return op == Op::Constant || op == Op::Unary || op == Op::Binary ||
               op == Op::SizeOf || op == Op::AlignOf;
    }

    static ValueId canonical(ValueId value, const std::unordered_map<ValueId, ValueId> &redirect) {
        auto found = redirect.find(value);
        while (found != redirect.end() && found->second != value) {
            value = found->second;
            found = redirect.find(value);
        }
        return value;
    }

    static std::string key(const Instruction &instruction) {
        std::ostringstream out;
        out << static_cast<int>(instruction.op) << '\0' << instruction.type.name << '\0'
            << instruction.detail;
        for (ValueId operand : instruction.operands) out << '\0' << operand;
        return out.str();
    }

    static bool number(Function &function) {
        bool changed = false;
        std::unordered_map<ValueId, ValueId> redirect;
        for (Block &block : function.blocks) {
            std::unordered_map<std::string, ValueId> available;
            std::vector<Instruction> kept;
            kept.reserve(block.instructions.size());
            for (Instruction instruction : block.instructions) {
                for (ValueId &operand : instruction.operands) operand = canonical(operand, redirect);
                if (instruction.result != NoValue && eligible(instruction.op)) {
                    const std::string expression = key(instruction);
                    auto found = available.find(expression);
                    if (found != available.end()) {
                        redirect[instruction.result] = found->second;
                        changed = true;
                        continue;
                    }
                    available.emplace(expression, instruction.result);
                }
                kept.push_back(std::move(instruction));
            }
            block.instructions = std::move(kept);
            if (block.terminator.value != NoValue)
                block.terminator.value = canonical(block.terminator.value, redirect);
        }
        return changed;
    }
};

// Remove stores proven dead inside one basic block. A load, address-taking
// operation, or block boundary is a conservative observation point. This is a
// deliberately alias-safe foundation for a future whole-function memory SSA
// pass rather than an optimistic global store deletion.
class LocalDeadStoreEliminationPass final : public Pass {
  public:
    const char *name() const override { return "local-dead-store-elimination"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) {
            for (Block &block : function.blocks) changed |= eliminate(block);
        }
        return changed;
    }

  private:
    static bool address_mentions(const Instruction &instruction, const std::string &name) {
        if (instruction.op != Op::AddressOf) return false;
        const std::size_t space = instruction.detail.find(' ');
        return space != std::string::npos && instruction.detail.substr(space + 1) == name;
    }

    static bool eliminate(Block &block) {
        std::unordered_map<std::string, std::size_t> pending_store;
        std::vector<bool> dead(block.instructions.size(), false);
        bool changed = false;
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const Instruction &instruction = block.instructions[index];
            if (instruction.op == Op::Load) {
                pending_store.erase(instruction.detail);
                continue;
            }
            if (instruction.op == Op::AddressOf) {
                for (auto it = pending_store.begin(); it != pending_store.end();) {
                    if (address_mentions(instruction, it->first)) it = pending_store.erase(it);
                    else ++it;
                }
                continue;
            }
            if (instruction.op != Op::Store) continue;
            auto previous = pending_store.find(instruction.detail);
            if (previous != pending_store.end()) {
                dead[previous->second] = true;
                changed = true;
            }
            pending_store[instruction.detail] = index;
        }
        if (!changed) return false;
        std::vector<Instruction> kept;
        kept.reserve(block.instructions.size());
        for (std::size_t index = 0; index < block.instructions.size(); ++index)
            if (!dead[index]) kept.push_back(std::move(block.instructions[index]));
        block.instructions = std::move(kept);
        return true;
    }
};

// Recursively delete unused, side-effect-free SSA producers. Operations which
// can allocate, call user code, observe memory, panic on bounds, or synchronize
// remain even when their result is ignored.
class DeadValueEliminationPass final : public Pass {
  public:
    const char *name() const override { return "dead-value-elimination"; }

    bool run(Program &program) override {
        bool changed = false;
        for (Function &function : program.functions) changed |= eliminate(function);
        return changed;
    }

  private:
    static bool pure(Op op) {
        return op == Op::Constant || op == Op::Unary || op == Op::Binary ||
               op == Op::SizeOf || op == Op::AlignOf || op == Op::AddressOf;
    }

    static bool eliminate(Function &function) {
        bool changed_any = false;
        for (;;) {
            std::unordered_set<ValueId> used;
            for (const Block &block : function.blocks) {
                for (const Instruction &instruction : block.instructions)
                    used.insert(instruction.operands.begin(), instruction.operands.end());
                if (block.terminator.value != NoValue) used.insert(block.terminator.value);
            }
            bool changed_round = false;
            for (Block &block : function.blocks) {
                std::vector<Instruction> kept;
                kept.reserve(block.instructions.size());
                for (Instruction &instruction : block.instructions) {
                    if (instruction.result != NoValue && !used.count(instruction.result) && pure(instruction.op)) {
                        changed_round = true;
                        continue;
                    }
                    kept.push_back(std::move(instruction));
                }
                block.instructions = std::move(kept);
            }
            changed_any |= changed_round;
            if (!changed_round) return changed_any;
        }
    }
};

class PassManager {
  public:
    template <typename T> void add() { passes_.push_back(std::make_unique<T>()); }

    bool run(Program &program) {
        bool changed = false;
        for (auto &pass : passes_) {
            changed |= pass->run(program);
            Lowerer::verify(program);
        }
        return changed;
    }

  private:
    std::vector<std::unique_ptr<Pass>> passes_;
};

inline bool optimize(Program &program) {
    PassManager passes;
    passes.add<ConstantFoldPass>();
    passes.add<LocalLoadForwardingPass>();
    passes.add<LocalValueNumberingPass>();
    passes.add<LocalDeadStoreEliminationPass>();
    passes.add<BranchSimplifyPass>();
    passes.add<DeadBlockEliminationPass>();
    passes.add<DeadValueEliminationPass>();
    return passes.run(program);
}

} // namespace pphir

#endif
