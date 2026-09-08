#ifndef PUNPUN_MIR_HPP
#define PUNPUN_MIR_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hir.hpp"

// PunPun MIR is the verified, mandatory middle-end authority between typed
// HIR and backend scheduling. It makes virtual registers, liveness intervals,
// allocation decisions, and stable function bodies explicit. The 0.6 native
// emitters still consult typed source nodes for final instruction/source-detail
// lowering, while the MIR function set/order and fingerprints are authoritative.
// 0.7 is reserved for a machine IR that removes that remaining source-detail
// dependency.
namespace ppmir {

using VReg = pphir::ValueId;
using BlockId = pphir::BlockId;

struct Location {
    enum class Kind { Register, Spill } kind = Kind::Spill;
    std::string name;
    std::size_t spill_slot = 0;
};

struct Interval {
    VReg value = pphir::NoValue;
    std::size_t start = 0;
    std::size_t end = 0;
    Type type = Type::Void;
    Location location;
};

struct Instruction {
    pphir::Op op = pphir::Op::Constant;
    VReg result = pphir::NoValue;
    Type type = Type::Void;
    std::string detail;
    std::vector<VReg> operands;
    Token token{TokenKind::End, "", "<mir>", 1, 1, 0, 0};
};

struct Terminator {
    pphir::Terminator::Kind kind = pphir::Terminator::Kind::None;
    VReg value = pphir::NoValue;
    BlockId first = pphir::NoBlock;
    BlockId second = pphir::NoBlock;
};

struct Block {
    BlockId id = 0;
    std::vector<Instruction> instructions;
    Terminator terminator;
};

struct Function {
    std::string name;
    Type result = Type::Void;
    bool external_native = false;
    bool is_async = false;
    std::vector<Block> blocks;
    std::vector<Interval> intervals;
    std::unordered_map<VReg, Location> locations;
    std::size_t spill_slots = 0;
};

struct Program { std::vector<Function> functions; };

class Lowerer {
  public:
    explicit Lowerer(const pphir::Program &hir) : hir_(hir) {}

    Program lower() const {
        Program program;
        program.functions.reserve(hir_.functions.size());
        for (const pphir::Function &source : hir_.functions)
            program.functions.push_back(lower_function(source));
        verify(program);
        return program;
    }

    static void verify(const Program &program) {
        for (const Function &function : program.functions) {
            if (function.external_native) continue;
            std::unordered_set<VReg> definitions;
            for (const Block &block : function.blocks) {
                for (const Instruction &instruction : block.instructions) {
                    for (VReg operand : instruction.operands)
                        if (!definitions.count(operand))
                            throw Error("internal compiler error: MIR use before definition in '" + function.name + "'");
                    if (instruction.result != pphir::NoValue) {
                        if (!definitions.insert(instruction.result).second)
                            throw Error("internal compiler error: duplicate MIR virtual register");
                        if (!function.locations.count(instruction.result))
                            throw Error("internal compiler error: MIR virtual register has no allocation");
                    }
                }
                if (block.terminator.value != pphir::NoValue && !definitions.count(block.terminator.value))
                    throw Error("internal compiler error: MIR terminator use before definition");
            }
        }
    }

  private:
    const pphir::Program &hir_;

    static bool float_value(Type type) { return type == Type::Float; }
    static bool register_value(Type type) {
        return type == Type::Int || type == Type::Bool || type == Type::Str || type == Type::Nums ||
               is_slice_type(type) || is_pointer_like_type(type) || is_task_type(type);
    }

    static Function lower_function(const pphir::Function &source) {
        Function result;
        result.name = source.name;
        result.result = source.result;
        result.external_native = source.external_native;
        result.is_async = source.is_async;
        for (const pphir::Block &source_block : source.blocks) {
            Block block;
            block.id = source_block.id;
            for (const pphir::Instruction &instruction : source_block.instructions)
                block.instructions.push_back({instruction.op, instruction.result, instruction.type,
                                              instruction.detail, instruction.operands, instruction.token});
            block.terminator = {source_block.terminator.kind, source_block.terminator.value,
                                source_block.terminator.first, source_block.terminator.second};
            result.blocks.push_back(std::move(block));
        }
        build_intervals(result, source.value_count);
        allocate(result);
        return result;
    }

    static void build_intervals(Function &function, std::size_t value_count) {
        const std::size_t missing = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> starts(value_count, missing), ends(value_count, 0);
        std::vector<Type> types(value_count, Type::Void);
        std::size_t position = 0;
        for (const Block &block : function.blocks) {
            for (const Instruction &instruction : block.instructions) {
                for (VReg operand : instruction.operands) if (operand < value_count) ends[operand] = std::max(ends[operand], position);
                if (instruction.result != pphir::NoValue) {
                    starts[instruction.result] = position;
                    ends[instruction.result] = position;
                    types[instruction.result] = instruction.type;
                }
                ++position;
            }
            if (block.terminator.value != pphir::NoValue && block.terminator.value < value_count)
                ends[block.terminator.value] = std::max(ends[block.terminator.value], position);
            ++position;
        }
        for (VReg value = 0; value < value_count; ++value)
            if (starts[value] != missing) function.intervals.push_back({value, starts[value], ends[value], types[value], {}});
    }

    static void allocate_class(Function &function, const std::vector<std::string> &registers,
                               bool want_float, bool want_register_values) {
        std::vector<Interval *> candidates;
        for (Interval &interval : function.intervals) {
            const bool matches = want_float ? float_value(interval.type) :
                (want_register_values && register_value(interval.type));
            if (matches) candidates.push_back(&interval);
        }
        std::sort(candidates.begin(), candidates.end(), [](const Interval *a, const Interval *b) {
            return a->start < b->start || (a->start == b->start && a->end < b->end);
        });
        std::vector<Interval *> active;
        for (Interval *interval : candidates) {
            active.erase(std::remove_if(active.begin(), active.end(), [&](const Interval *other) {
                return other->end < interval->start;
            }), active.end());
            std::unordered_set<std::string> occupied;
            for (const Interval *other : active)
                if (other->location.kind == Location::Kind::Register) occupied.insert(other->location.name);
            auto free = std::find_if(registers.begin(), registers.end(), [&](const std::string &name) {
                return !occupied.count(name);
            });
            if (free != registers.end()) interval->location = {Location::Kind::Register, *free, 0};
            else interval->location = {Location::Kind::Spill, "", function.spill_slots++};
            active.push_back(interval);
        }
    }

    static void allocate(Function &function) {
        allocate_class(function, {"r10", "r11", "r12", "r13", "r14", "r15"}, false, true);
        allocate_class(function, {"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13"}, true, false);
        for (Interval &interval : function.intervals) {
            if (!float_value(interval.type) && !register_value(interval.type))
                interval.location = {Location::Kind::Spill, "", function.spill_slots++};
            function.locations[interval.value] = interval.location;
        }
    }
};

inline std::string location_name(const Location &location) {
    return location.kind == Location::Kind::Register ? location.name : "spill[" + std::to_string(location.spill_slot) + "]";
}

inline std::string dump(const Program &program) {
    std::ostringstream out;
    for (const Function &function : program.functions) {
        out << "mir.func @" << function.name << " -> " << type_name(function.result);
        if (function.is_async) out << " async";
        if (function.external_native) { out << " extern-native\n"; continue; }
        out << " spills=" << function.spill_slots << " {\n";
        for (const Block &block : function.blocks) {
            out << "  bb" << block.id << ":\n";
            for (const Instruction &instruction : block.instructions) {
                out << "    ";
                if (instruction.result != pphir::NoValue)
                    out << "v" << instruction.result << ":" << type_name(instruction.type) << "@"
                        << location_name(function.locations.at(instruction.result)) << " = ";
                out << pphir::op_name(instruction.op);
                if (!instruction.detail.empty()) out << " " << instruction.detail;
                for (VReg operand : instruction.operands) out << " v" << operand;
                out << "\n";
            }
            const Terminator &term = block.terminator;
            out << "    ";
            if (term.kind == pphir::Terminator::Kind::Jump) out << "jump bb" << term.first;
            else if (term.kind == pphir::Terminator::Kind::Branch)
                out << "branch v" << term.value << ", bb" << term.first << ", bb" << term.second;
            else if (term.kind == pphir::Terminator::Kind::Return) {
                out << "return"; if (term.value != pphir::NoValue) out << " v" << term.value;
            } else out << "unreachable";
            out << "\n";
        }
        out << "  liveness:\n";
        for (const Interval &interval : function.intervals)
            out << "    v" << interval.value << " [" << interval.start << "," << interval.end << "] -> "
                << location_name(interval.location) << "\n";
        out << "}\n";
    }
    return out.str();
}

} // namespace ppmir

#endif
