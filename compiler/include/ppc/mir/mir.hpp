#ifndef PPC_MIR_MIR_HPP
#define PPC_MIR_MIR_HPP

#include <string>
#include <vector>

#include "ppc/sema/type.hpp"
#include "ppc/support/common.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

/// Virtual register. Every one is assigned exactly once by construction, so
/// value numbering and copy propagation can treat a register as its definition.
using Reg = u32;
constexpr Reg kNoReg = 0xFFFFFFFFu;

/// Basic block index inside a MirFunction.
using BlockId = u32;
constexpr BlockId kNoBlock = 0xFFFFFFFFu;

enum class MirOp : u8 {
    // Constants
    ConstInt,
    ConstFloat,
    ConstStr,
    ConstBool,

    // Locals are memory slots; mem2reg promotes the ones that never escape.
    LoadLocal,
    StoreLocal,
    LocalAddr,  // &local

    // Arithmetic and logic
    Binary,
    Unary,

    // Calls / closures
    Call,
    /// Allocates a closure object. `target` is the function specialization and
    /// `args` are captured values in environment order.
    MakeClosure,
    /// Reads/writes a captured slot of the current closure. `index` is the
    /// environment slot.
    LoadCapture,
    StoreCapture,
    /// Call through a register holding a closure object.
    CallIndirect,
    CallBuiltin,

    // Aggregates
    MakeStruct,
    GetField,
    SetField,
    MakeEnum,
    EnumTag,
    EnumPayload,
    MakeList,

    // Indexing
    GetIndex,
    SetIndex,

    // Pointers
    Deref,
    StoreDeref,

    // Async
    Await,

    // Deterministic destruction
    Drop,

    // Terminators
    Jump,
    Branch,
    Return,
};

/// One three-address instruction.
///
/// Terminators (Jump, Branch, Return) only ever appear as the last instruction
/// of a block; `MirFunction::verify` enforces that.
struct MirInst {
    MirOp op = MirOp::ConstInt;
    Span span;

    Reg dest = kNoReg;
    Reg a = kNoReg;
    Reg b = kNoReg;
    Reg c = kNoReg;

    i64 imm = 0;
    double fimm = 0.0;
    Symbol text;

    /// Call target (specialization index), builtin id, struct/enum decl index.
    u32 target = 0;
    /// Field index, enum variant index, or local slot.
    u32 index = 0;
    /// Payload slot inside a variant.
    u32 slot = 0;

    BinaryOp binary_op = BinaryOp::Add;
    UnaryOp unary_op = UnaryOp::Negate;

    std::vector<Reg> args;

    BlockId then_block = kNoBlock;
    BlockId else_block = kNoBlock;

    /// Static type of `dest`, needed by the backends to pick a representation.
    const Type *type = nullptr;
    /// CallIndirect: the callee's function type, which carries the signature.
    const Type *callee_type = nullptr;

    bool is_terminator() const {
        return op == MirOp::Jump || op == MirOp::Branch || op == MirOp::Return;
    }
    /// True when removing this instruction could change observable behavior.
    bool has_side_effects() const {
        switch (op) {
            case MirOp::StoreLocal:
            case MirOp::SetField:
            case MirOp::SetIndex:
            case MirOp::StoreDeref:
            case MirOp::StoreCapture:
            case MirOp::Call:
            case MirOp::MakeClosure:
            case MirOp::CallIndirect:
            case MirOp::CallBuiltin:
            case MirOp::Await:
            case MirOp::Drop:
            case MirOp::Jump:
            case MirOp::Branch:
            case MirOp::Return:
                return true;
            default:
                return false;
        }
    }
};

struct MirBlock {
    BlockId id = 0;
    std::vector<MirInst> instructions;
    /// Filled in by `compute_cfg`.
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;
    bool reachable = false;
};

struct MirLocal {
    Symbol name;
    const Type *type = nullptr;
    bool is_parameter = false;
    /// Set when the local's address is taken, which blocks promotion.
    bool address_taken = false;
};

struct MirCapture {
    u32 local = 0;
    const Type *type = nullptr;
    bool is_mutable = false;
};

struct MirFunction {
    std::string name;
    Span span;
    std::vector<MirLocal> locals;
    u32 param_count = 0;
    const Type *result = nullptr;
    std::vector<MirBlock> blocks;
    /// Type of each virtual register, indexed by Reg.
    std::vector<const Type *> reg_types;
    std::vector<MirCapture> captures;

    bool is_entry = false;
    bool is_async = false;
    bool is_extern_native = false;
    std::string native_symbol;

    BlockId entry_block = 0;

    BlockId add_block();
    Reg add_reg(const Type *type);
    /// Recomputes predecessor/successor edges and reachability.
    void compute_cfg();
    /// Structural sanity check. Returns an empty string when the function is
    /// well formed, otherwise a description of the first problem found.
    std::string verify() const;
};

struct MirProgram {
    std::vector<MirFunction *> functions;
    u32 entry = 0xFFFFFFFFu;
    std::vector<InjectionDecl> injections;
};

/// Renders MIR as text for `ppc emit-mir`.
std::string dump_mir(const MirProgram &program, const TypeContext &types);

}  // namespace ppc

#endif
