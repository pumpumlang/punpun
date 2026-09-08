#ifndef PUNPUN_MACHINE_IR_HPP
#define PUNPUN_MACHINE_IR_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mir.hpp"

// PunPun Machine IR (MIR2) is the target-aware layer introduced in 0.7.
//
// The older verified MIR remains target-independent and preserves the language
// operations. Machine IR adds the pieces that a native backend must agree on:
// target calling convention, argument/result placement, call-clobber-aware
// liveness, physical-register choices, spill slots, stack-frame requirements,
// and a verifier that rejects overlapping allocations.
//
// Step 7 intentionally introduces this layer before deleting every final AST
// detail dependency from the emitters. The direct x86 backend consumes Machine
// IR for function authority and PunPun/native ABI layout immediately; later 0.7
// work moves individual source-detail lowering operations behind this boundary.
namespace ppmachine {

using VReg = ppmir::VReg;
using BlockId = ppmir::BlockId;

constexpr VReg NoValue = pphir::NoValue;
constexpr BlockId NoBlock = pphir::NoBlock;

inline std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}

enum class Target { X86_64SysV };

inline const char *target_name(Target target) {
    switch (target) {
        case Target::X86_64SysV: return "x86_64-sysv";
    }
    return "unknown";
}

enum class ValueClass { Gpr, Fpr, Aggregate };

inline const char *value_class_name(ValueClass value_class) {
    switch (value_class) {
        case ValueClass::Gpr: return "gpr";
        case ValueClass::Fpr: return "fpr";
        case ValueClass::Aggregate: return "aggregate";
    }
    return "?";
}

enum class CallingConvention { PunPunBlock, SysVAMD64 };

inline const char *calling_convention_name(CallingConvention convention) {
    switch (convention) {
        case CallingConvention::PunPunBlock: return "punpun-block";
        case CallingConvention::SysVAMD64: return "sysv-amd64";
    }
    return "?";
}

enum class AbiLocationKind {
    None,
    ArgumentBlock,
    Register,
    Stack,
    HiddenResultPointer
};

struct AbiLocation {
    AbiLocationKind kind = AbiLocationKind::None;
    std::string name;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct AbiValue {
    Type type = Type::Void;
    AbiLocation location;
};

struct FunctionABI {
    CallingConvention convention = CallingConvention::PunPunBlock;
    bool hidden_result_pointer = false;
    std::size_t argument_block_size = 0;
    std::vector<AbiValue> parameters;
    AbiValue result;
};

inline std::string abi_location_name(const AbiLocation &location) {
    switch (location.kind) {
        case AbiLocationKind::None: return "none";
        case AbiLocationKind::ArgumentBlock:
            return "argblock+" + std::to_string(location.offset);
        case AbiLocationKind::Register:
            return location.name;
        case AbiLocationKind::Stack:
            return "stack+" + std::to_string(location.offset);
        case AbiLocationKind::HiddenResultPointer:
            if (!location.name.empty()) return "sret:" + location.name;
            return "sret:argblock+" + std::to_string(location.offset);
    }
    return "?";
}

struct Location {
    enum class Kind { Register, Stack } kind = Kind::Stack;
    std::string name;
    std::size_t stack_slot = 0;
    std::size_t stack_slots = 1;
};

inline std::string location_name(const Location &location) {
    if (location.kind == Location::Kind::Register) return location.name;
    if (location.stack_slots <= 1) return "stack[" + std::to_string(location.stack_slot) + "]";
    return "stack[" + std::to_string(location.stack_slot) + ".." +
           std::to_string(location.stack_slot + location.stack_slots - 1) + "]";
}

struct Interval {
    VReg value = NoValue;
    std::size_t start = 0;
    std::size_t end = 0;
    Type type = Type::Void;
    ValueClass value_class = ValueClass::Gpr;
    std::size_t size = 8;
    bool crosses_call = false;
    Location location;
};

struct CallABI {
    bool known_function = false;
    CallingConvention convention = CallingConvention::PunPunBlock;
    bool hidden_result_pointer = false;
    std::size_t argument_block_size = 0;
    std::vector<AbiValue> arguments;
    AbiValue result;
};

struct Instruction {
    pphir::Op op = pphir::Op::Constant;
    VReg result = NoValue;
    Type type = Type::Void;
    std::string detail;
    std::vector<VReg> operands;
    Token token{TokenKind::End, "", "<machine-ir>", 1, 1, 0, 0};
    bool call_barrier = false;
    CallABI call;
};

struct Terminator {
    pphir::Terminator::Kind kind = pphir::Terminator::Kind::None;
    VReg value = NoValue;
    BlockId first = NoBlock;
    BlockId second = NoBlock;
};

struct Block {
    BlockId id = 0;
    std::vector<Instruction> instructions;
    Terminator terminator;
};

struct Function {
    std::string name;
    std::string native_symbol;
    Type result = Type::Void;
    bool external_native = false;
    bool is_async = false;
    std::vector<std::pair<std::string, Type>> parameters;
    FunctionABI abi;
    std::vector<Block> blocks;
    std::vector<Interval> intervals;
    std::unordered_map<VReg, Location> locations;
    std::vector<std::string> callee_saved_registers;
    std::size_t stack_slots = 0;
    std::size_t stack_frame_bytes = 0;
};

struct Program {
    Target target = Target::X86_64SysV;
    std::vector<Function> functions;
};

// Mirrors the existing direct backend's layout rules. All scalar/reference
// values occupy eight bytes. Record fields are tightly laid out in declaration
// order in 8-byte units; an empty record still occupies one word.
class LayoutTable {
  public:
    explicit LayoutTable(const std::vector<Module> &modules) {
        for (const Module &module : modules)
            for (const Shape &shape : module.shapes) shapes_[shape.name] = &shape;
    }

    bool is_record(const Type &type) const {
        const auto found = shapes_.find(type.name);
        return found != shapes_.end() && !found->second->reference_type;
    }

    ValueClass classify(const Type &type) const {
        if (type == Type::Float) return ValueClass::Fpr;
        if (is_record(type)) return ValueClass::Aggregate;
        return ValueClass::Gpr;
    }

    std::size_t size_of(const Type &type) const {
        std::unordered_set<std::string> active;
        return size_of(type, active);
    }

  private:
    std::unordered_map<std::string, const Shape *> shapes_;

    std::size_t size_of(const Type &type, std::unordered_set<std::string> &active) const {
        if (type == Type::Void) return 0;
        const auto found = shapes_.find(type.name);
        if (found == shapes_.end() || found->second->reference_type) return 8;
        if (!active.insert(type.name).second)
            throw Error("internal compiler error: recursive by-value record reached Machine IR layout: " + type.name);
        std::size_t size = 0;
        for (const Parameter &field : found->second->fields)
            size += align_up(size_of(field.type, active), 8);
        active.erase(type.name);
        return size == 0 ? 8 : size;
    }
};

class Lowerer {
  public:
    Lowerer(const ppmir::Program &mir, const std::vector<Module> &modules,
            Target target = Target::X86_64SysV)
        : mir_(mir), layouts_(modules), target_(target) {}

    Program lower() const {
        Program program;
        program.target = target_;
        program.functions.reserve(mir_.functions.size());

        std::unordered_map<std::string, FunctionABI> abis;
        for (const ppmir::Function &source : mir_.functions)
            abis.emplace(source.name, lower_abi(source));

        for (const ppmir::Function &source : mir_.functions)
            program.functions.push_back(lower_function(source, abis));

        verify(program);
        return program;
    }

    static void verify(const Program &program) {
        std::unordered_set<std::string> names;
        for (const Function &function : program.functions) {
            if (!names.insert(function.name).second)
                fail("duplicate Machine IR function '" + function.name + "'");
            if (function.parameters.size() != function.abi.parameters.size())
                fail("ABI parameter count mismatch in '" + function.name + "'");
            if (function.stack_frame_bytes % 16 != 0)
                fail("Machine IR frame is not 16-byte aligned in '" + function.name + "'");
            if (function.external_native) continue;
            if (function.blocks.empty()) fail("Machine IR function has no entry block: " + function.name);

            std::unordered_set<VReg> definitions;
            for (std::size_t expected = 0; expected < function.blocks.size(); ++expected) {
                const Block &block = function.blocks[expected];
                if (block.id != expected) fail("non-canonical Machine IR block numbering");
                for (const Instruction &instruction : block.instructions) {
                    for (VReg operand : instruction.operands)
                        if (!definitions.count(operand))
                            fail("Machine IR use before definition in '" + function.name + "'");
                    if (instruction.result != NoValue) {
                        if (!definitions.insert(instruction.result).second)
                            fail("duplicate Machine IR virtual register");
                        if (!function.locations.count(instruction.result))
                            fail("Machine IR virtual register has no location");
                    }
                }
                const Terminator &term = block.terminator;
                if (term.kind == pphir::Terminator::Kind::None)
                    fail("unterminated Machine IR block in '" + function.name + "'");
                if (term.kind == pphir::Terminator::Kind::Jump) check_block(term.first, function.blocks.size());
                if (term.kind == pphir::Terminator::Kind::Branch) {
                    if (!definitions.count(term.value)) fail("Machine IR branch uses undefined value");
                    check_block(term.first, function.blocks.size());
                    check_block(term.second, function.blocks.size());
                }
                if (term.kind == pphir::Terminator::Kind::Return && term.value != NoValue &&
                    !definitions.count(term.value))
                    fail("Machine IR return uses undefined value");
            }

            for (const Interval &interval : function.intervals) {
                if (interval.location.kind == Location::Kind::Register) {
                    if (interval.crosses_call && caller_saved(interval.location.name))
                        fail("call-live value assigned to caller-saved register in '" + function.name + "'");
                    if (interval.crosses_call && interval.value_class == ValueClass::Fpr)
                        fail("call-live floating value assigned to volatile XMM register in '" + function.name + "'");
                } else if (interval.location.stack_slot + interval.location.stack_slots > function.stack_slots) {
                    fail("Machine IR spill exceeds frame in '" + function.name + "'");
                }
            }

            for (std::size_t i = 0; i < function.intervals.size(); ++i) {
                for (std::size_t j = i + 1; j < function.intervals.size(); ++j) {
                    const Interval &left = function.intervals[i];
                    const Interval &right = function.intervals[j];
                    if (!overlap(left.start, left.end, right.start, right.end)) continue;
                    if (left.location.kind == Location::Kind::Register &&
                        right.location.kind == Location::Kind::Register &&
                        left.location.name == right.location.name)
                        fail("overlapping Machine IR intervals share register '" + left.location.name + "'");
                    if (left.location.kind == Location::Kind::Stack &&
                        right.location.kind == Location::Kind::Stack &&
                        stack_overlap(left.location, right.location))
                        fail("overlapping Machine IR intervals share spill slot");
                }
            }
        }
    }

  private:
    const ppmir::Program &mir_;
    LayoutTable layouts_;
    Target target_;

    [[noreturn]] static void fail(const std::string &message) {
        throw Error("internal compiler error: " + message);
    }

    static bool overlap(std::size_t a_start, std::size_t a_end,
                        std::size_t b_start, std::size_t b_end) {
        return a_start <= b_end && b_start <= a_end;
    }

    static bool stack_overlap(const Location &left, const Location &right) {
        const std::size_t left_end = left.stack_slot + left.stack_slots;
        const std::size_t right_end = right.stack_slot + right.stack_slots;
        return left.stack_slot < right_end && right.stack_slot < left_end;
    }

    static void check_block(BlockId block, std::size_t count) {
        if (block == NoBlock || block >= count)
            fail("Machine IR terminator targets invalid block");
    }

    static bool caller_saved(const std::string &name) {
        static const std::unordered_set<std::string> registers{
            "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
            "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
        };
        return registers.count(name) != 0;
    }

    static bool callee_saved(const std::string &name) {
        return name == "rbx" || name == "r12" || name == "r13" || name == "r14" || name == "r15";
    }

    bool record(const Type &type) const { return layouts_.is_record(type); }

    FunctionABI lower_abi(const ppmir::Function &source) const {
        FunctionABI abi;
        if (!source.external_native) {
            abi.convention = CallingConvention::PunPunBlock;
            std::size_t offset = record(source.result) ? 8 : 0;
            abi.hidden_result_pointer = record(source.result);
            for (const auto &[name, type] : source.parameters) {
                (void)name;
                const std::size_t size = align_up(layouts_.size_of(type), 8);
                abi.parameters.push_back({type, {AbiLocationKind::ArgumentBlock, "", offset, size}});
                offset += size;
            }
            abi.argument_block_size = offset;
            if (source.result == Type::Void) {
                abi.result = {Type::Void, {AbiLocationKind::None, "", 0, 0}};
            } else if (abi.hidden_result_pointer) {
                abi.result = {source.result, {AbiLocationKind::HiddenResultPointer, "", 0, layouts_.size_of(source.result)}};
            } else {
                // PunPun's private ABI deliberately returns every scalar,
                // including float bit patterns, through RAX in 0.7 dev.1.
                abi.result = {source.result, {AbiLocationKind::Register, "rax", 0, 8}};
            }
            return abi;
        }

        abi.convention = CallingConvention::SysVAMD64;
        static const std::vector<std::string> gpr_args{"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        static const std::vector<std::string> fpr_args{"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
        std::size_t gpr = 0;
        std::size_t fpr = 0;
        std::size_t stack = 0;
        for (const auto &[name, type] : source.parameters) {
            (void)name;
            const ValueClass value_class = layouts_.classify(type);
            if (value_class == ValueClass::Fpr && fpr < fpr_args.size()) {
                abi.parameters.push_back({type, {AbiLocationKind::Register, fpr_args[fpr++], 0, 8}});
            } else if (value_class == ValueClass::Gpr && gpr < gpr_args.size()) {
                abi.parameters.push_back({type, {AbiLocationKind::Register, gpr_args[gpr++], 0, 8}});
            } else {
                const std::size_t size = align_up(layouts_.size_of(type), 8);
                abi.parameters.push_back({type, {AbiLocationKind::Stack, "", stack, size}});
                stack += size;
            }
        }
        abi.argument_block_size = stack;
        if (source.result == Type::Void) {
            abi.result = {Type::Void, {AbiLocationKind::None, "", 0, 0}};
        } else if (record(source.result)) {
            abi.hidden_result_pointer = true;
            abi.result = {source.result, {AbiLocationKind::HiddenResultPointer, "rdi", 0, layouts_.size_of(source.result)}};
        } else if (source.result == Type::Float) {
            abi.result = {source.result, {AbiLocationKind::Register, "xmm0", 0, 8}};
        } else {
            abi.result = {source.result, {AbiLocationKind::Register, "rax", 0, 8}};
        }
        return abi;
    }

    static bool is_call_barrier(pphir::Op op) {
        // These operations either directly issue a call today or can lower to
        // runtime helpers. Treating them as barriers is intentionally
        // conservative: preserving a value is cheaper than miscompiling one.
        switch (op) {
            case pphir::Op::Call:
            case pphir::Op::Construct:
            case pphir::Op::EnumConstruct:
            case pphir::Op::Match:
            case pphir::Op::Propagate:
            case pphir::Op::Await:
            case pphir::Op::List:
            case pphir::Op::Say:
                return true;
            default:
                return false;
        }
    }

    Function lower_function(const ppmir::Function &source,
                            const std::unordered_map<std::string, FunctionABI> &abis) const {
        Function result;
        result.name = source.name;
        result.native_symbol = source.native_symbol;
        result.result = source.result;
        result.external_native = source.external_native;
        result.is_async = source.is_async;
        result.parameters = source.parameters;
        result.abi = abis.at(source.name);

        for (const ppmir::Block &source_block : source.blocks) {
            Block block;
            block.id = source_block.id;
            for (const ppmir::Instruction &source_instruction : source_block.instructions) {
                Instruction instruction;
                instruction.op = source_instruction.op;
                instruction.result = source_instruction.result;
                instruction.type = source_instruction.type;
                instruction.detail = source_instruction.detail;
                instruction.operands = source_instruction.operands;
                instruction.token = source_instruction.token;
                instruction.call_barrier = is_call_barrier(source_instruction.op);
                if (source_instruction.op == pphir::Op::Call) {
                    const auto found = abis.find(source_instruction.detail);
                    if (found != abis.end()) {
                        instruction.call.known_function = true;
                        instruction.call.convention = found->second.convention;
                        instruction.call.hidden_result_pointer = found->second.hidden_result_pointer;
                        instruction.call.argument_block_size = found->second.argument_block_size;
                        instruction.call.arguments = found->second.parameters;
                        instruction.call.result = found->second.result;
                    }
                }
                block.instructions.push_back(std::move(instruction));
            }
            block.terminator = {source_block.terminator.kind, source_block.terminator.value,
                                source_block.terminator.first, source_block.terminator.second};
            result.blocks.push_back(std::move(block));
        }

        if (!result.external_native) {
            build_intervals(result);
            allocate(result);
        }
        return result;
    }

    void build_intervals(Function &function) const {
        VReg maximum = 0;
        bool any_value = false;
        for (const Block &block : function.blocks)
            for (const Instruction &instruction : block.instructions)
                if (instruction.result != NoValue) {
                    maximum = std::max(maximum, instruction.result);
                    any_value = true;
                }
        if (!any_value) return;
        const std::size_t count = maximum + 1;
        const std::size_t missing = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> starts(count, missing), ends(count, 0);
        std::vector<Type> types(count, Type::Void);
        std::vector<std::size_t> barriers;

        std::size_t position = 0;
        for (const Block &block : function.blocks) {
            for (const Instruction &instruction : block.instructions) {
                for (VReg operand : instruction.operands)
                    if (operand < count) ends[operand] = std::max(ends[operand], position);
                if (instruction.result != NoValue) {
                    starts[instruction.result] = position;
                    ends[instruction.result] = position;
                    types[instruction.result] = instruction.type;
                }
                if (instruction.call_barrier) barriers.push_back(position);
                ++position;
            }
            if (block.terminator.value != NoValue && block.terminator.value < count)
                ends[block.terminator.value] = std::max(ends[block.terminator.value], position);
            ++position;
        }

        for (VReg value = 0; value < count; ++value) {
            if (starts[value] == missing) continue;
            bool crosses = false;
            for (std::size_t barrier : barriers) {
                if (starts[value] < barrier && barrier < ends[value]) {
                    crosses = true;
                    break;
                }
            }
            const std::size_t size = std::max<std::size_t>(8, align_up(layouts_.size_of(types[value]), 8));
            function.intervals.push_back({value, starts[value], ends[value], types[value],
                                          layouts_.classify(types[value]), size, crosses, {}});
        }
    }

    static std::vector<std::string> allowed_registers(const Interval &interval) {
        if (interval.value_class == ValueClass::Aggregate) return {};
        if (interval.value_class == ValueClass::Fpr) {
            if (interval.crosses_call) return {};
            return {"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"};
        }
        if (interval.crosses_call) return {"r12", "r13", "r14", "r15", "rbx"};
        return {"r10", "r11", "r8", "r9", "rcx", "rdx", "r12", "r13", "r14", "r15", "rbx"};
    }

    static void expire(std::vector<Interval *> &active, std::size_t start) {
        active.erase(std::remove_if(active.begin(), active.end(), [start](const Interval *interval) {
            return interval->end < start || interval->location.kind != Location::Kind::Register;
        }), active.end());
    }

    static void allocate_registers(std::vector<Interval *> candidates) {
        std::sort(candidates.begin(), candidates.end(), [](const Interval *left, const Interval *right) {
            if (left->start != right->start) return left->start < right->start;
            if (left->end != right->end) return left->end < right->end;
            return left->value < right->value;
        });

        std::vector<Interval *> active;
        for (Interval *interval : candidates) {
            expire(active, interval->start);
            const std::vector<std::string> allowed = allowed_registers(*interval);
            if (allowed.empty()) {
                interval->location = {Location::Kind::Stack, "", 0, 1};
                continue;
            }

            std::unordered_set<std::string> occupied;
            for (const Interval *other : active) occupied.insert(other->location.name);
            auto free = std::find_if(allowed.begin(), allowed.end(), [&](const std::string &name) {
                return !occupied.count(name);
            });
            if (free != allowed.end()) {
                interval->location = {Location::Kind::Register, *free, 0, 0};
                active.push_back(interval);
                continue;
            }

            // Linear-scan eviction: if an active value lives substantially
            // longer than the newcomer, spill the farthest-ending compatible
            // interval and let the short-lived value use its register.
            Interval *victim = nullptr;
            for (Interval *other : active) {
                if (std::find(allowed.begin(), allowed.end(), other->location.name) == allowed.end()) continue;
                if (!victim || other->end > victim->end) victim = other;
            }
            if (victim && victim->end > interval->end) {
                const std::string register_name = victim->location.name;
                victim->location = {Location::Kind::Stack, "", 0, 1};
                interval->location = {Location::Kind::Register, register_name, 0, 0};
                active.erase(std::remove(active.begin(), active.end(), victim), active.end());
                active.push_back(interval);
            } else {
                interval->location = {Location::Kind::Stack, "", 0, 1};
            }
        }
    }

    struct ReusableSlot {
        std::size_t slot = 0;
        std::size_t end = 0;
    };

    static std::size_t find_reusable_scalar_slot(const std::vector<ReusableSlot> &slots,
                                                 std::size_t start) {
        for (std::size_t index = 0; index < slots.size(); ++index)
            if (slots[index].end < start) return index;
        return slots.size();
    }

    static void assign_stack_slots(Function &function) {
        std::vector<Interval *> spilled;
        for (Interval &interval : function.intervals)
            if (interval.location.kind == Location::Kind::Stack) spilled.push_back(&interval);
        std::sort(spilled.begin(), spilled.end(), [](const Interval *left, const Interval *right) {
            if (left->start != right->start) return left->start < right->start;
            return left->value < right->value;
        });

        std::vector<ReusableSlot> scalar_slots;
        std::size_t next_slot = 0;
        for (Interval *interval : spilled) {
            const std::size_t needed = std::max<std::size_t>(1, align_up(interval->size, 8) / 8);
            if (interval->value_class != ValueClass::Aggregate && needed == 1) {
                const std::size_t reusable = find_reusable_scalar_slot(scalar_slots, interval->start);
                if (reusable == scalar_slots.size()) {
                    const std::size_t slot = next_slot++;
                    scalar_slots.push_back({slot, interval->end});
                    interval->location = {Location::Kind::Stack, "", slot, 1};
                } else {
                    scalar_slots[reusable].end = interval->end;
                    interval->location = {Location::Kind::Stack, "", scalar_slots[reusable].slot, 1};
                }
            } else {
                interval->location = {Location::Kind::Stack, "", next_slot, needed};
                next_slot += needed;
            }
        }
        function.stack_slots = next_slot;
        function.stack_frame_bytes = align_up(function.stack_slots * 8, 16);
    }

    static void allocate(Function &function) {
        std::vector<Interval *> gpr;
        std::vector<Interval *> fpr;
        for (Interval &interval : function.intervals) {
            if (interval.value_class == ValueClass::Gpr) gpr.push_back(&interval);
            else if (interval.value_class == ValueClass::Fpr) fpr.push_back(&interval);
            else interval.location = {Location::Kind::Stack, "", 0, 1};
        }
        allocate_registers(std::move(gpr));
        allocate_registers(std::move(fpr));
        assign_stack_slots(function);

        std::unordered_set<std::string> preserved;
        for (Interval &interval : function.intervals) {
            function.locations[interval.value] = interval.location;
            if (interval.location.kind == Location::Kind::Register && callee_saved(interval.location.name))
                preserved.insert(interval.location.name);
        }
        static const std::vector<std::string> canonical{"rbx", "r12", "r13", "r14", "r15"};
        for (const std::string &name : canonical)
            if (preserved.count(name)) function.callee_saved_registers.push_back(name);
    }
};

inline std::string dump(const Program &program) {
    std::ostringstream out;
    out << "machine.target " << target_name(program.target) << "\n";
    for (const Function &function : program.functions) {
        out << "machine.func @" << function.name << " cc=" << calling_convention_name(function.abi.convention)
            << " -> " << type_name(function.result);
        if (function.is_async) out << " async";
        if (function.external_native) {
            out << " extern-native";
            if (!function.native_symbol.empty()) out << " @" << function.native_symbol;
        }
        out << "\n";
        out << "  abi args=" << function.abi.argument_block_size
            << " hidden-result=" << (function.abi.hidden_result_pointer ? "yes" : "no") << "\n";
        for (std::size_t i = 0; i < function.abi.parameters.size(); ++i) {
            out << "    arg" << i;
            if (i < function.parameters.size()) out << " " << function.parameters[i].first;
            out << ":" << type_name(function.abi.parameters[i].type) << " -> "
                << abi_location_name(function.abi.parameters[i].location)
                << " size=" << function.abi.parameters[i].location.size << "\n";
        }
        if (function.result != Type::Void)
            out << "    result:" << type_name(function.result) << " -> "
                << abi_location_name(function.abi.result.location)
                << " size=" << function.abi.result.location.size << "\n";
        if (function.external_native) continue;

        out << "  frame slots=" << function.stack_slots << " bytes=" << function.stack_frame_bytes;
        if (!function.callee_saved_registers.empty()) {
            out << " saved=";
            for (std::size_t i = 0; i < function.callee_saved_registers.size(); ++i) {
                if (i) out << ',';
                out << function.callee_saved_registers[i];
            }
        }
        out << "\n";
        for (const Block &block : function.blocks) {
            out << "  bb" << block.id << ":\n";
            for (const Instruction &instruction : block.instructions) {
                out << "    ";
                if (instruction.result != NoValue) {
                    out << "v" << instruction.result << ":" << type_name(instruction.type) << "@"
                        << location_name(function.locations.at(instruction.result)) << " = ";
                }
                out << pphir::op_name(instruction.op);
                if (!instruction.detail.empty()) out << " " << instruction.detail;
                for (VReg operand : instruction.operands) out << " v" << operand;
                if (instruction.call_barrier) out << " [call-barrier]";
                if (instruction.call.known_function)
                    out << " [cc=" << calling_convention_name(instruction.call.convention)
                        << " args=" << instruction.call.argument_block_size << "]";
                out << "\n";
            }
            const Terminator &term = block.terminator;
            out << "    ";
            if (term.kind == pphir::Terminator::Kind::Jump) out << "jump bb" << term.first;
            else if (term.kind == pphir::Terminator::Kind::Branch)
                out << "branch v" << term.value << ", bb" << term.first << ", bb" << term.second;
            else if (term.kind == pphir::Terminator::Kind::Return) {
                out << "return";
                if (term.value != NoValue) out << " v" << term.value;
            } else if (term.kind == pphir::Terminator::Kind::Unreachable) out << "unreachable";
            else out << "none";
            out << "\n";
        }
        out << "  allocation:\n";
        for (const Interval &interval : function.intervals) {
            out << "    v" << interval.value << " [" << interval.start << ',' << interval.end << "] "
                << value_class_name(interval.value_class)
                << " call-live=" << (interval.crosses_call ? "yes" : "no")
                << " -> " << location_name(interval.location) << "\n";
        }
    }
    return out.str();
}

} // namespace ppmachine

#endif
