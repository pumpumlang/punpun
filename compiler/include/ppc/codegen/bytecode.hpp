#ifndef PPC_CODEGEN_BYTECODE_HPP
#define PPC_CODEGEN_BYTECODE_HPP

#include <string>
#include <vector>

#include "ppc/support/common.hpp"

namespace ppc {

/// Bytecode operation.
///
/// The VM is register-based rather than stack-based, which keeps the mapping
/// from MIR almost one-to-one and avoids the push/pop traffic a stack machine
/// spends on every expression. The hot integer and comparison operations get
/// their own opcodes; colder ones share a generic form with the operation in
/// `imm`, because dispatch cost only matters where the loop actually spins.
enum class Op : u8 {
    Halt,

    // Constants and data movement
    ConstInt,
    ConstFloat,
    ConstStr,
    ConstBool,
    Move,
    /// Deep-copies a value struct. The VM boxes every aggregate, so a plain
    /// slot copy would give a `struct` reference semantics; this restores the
    /// value semantics the language specifies. Nested value fields are copied
    /// too, which is what makes `a.inner.x = 1` leave the original alone.
    CopyStruct,
    LoadLocal,
    StoreLocal,
    LocalAddr,

    // Checked integer arithmetic. These trap on overflow, matching the
    // language spec; UncheckedAdd and friends are selected only under
    // --unchecked.
    AddInt,
    SubInt,
    MulInt,
    DivInt,
    ModInt,
    NegInt,
    UncheckedAddInt,
    UncheckedSubInt,
    UncheckedMulInt,

    // Float arithmetic
    AddFloat,
    SubFloat,
    MulFloat,
    DivFloat,
    NegFloat,

    // Bitwise
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
    BitNot,

    // Comparison. Integer and boolean values share the integer forms because
    // both are stored as int64 in a slot.
    EqInt,
    NeInt,
    LtInt,
    LeInt,
    GtInt,
    GeInt,
    EqFloat,
    NeFloat,
    LtFloat,
    LeFloat,
    GtFloat,
    GeFloat,
    EqStr,
    NeStr,
    LtStr,
    LeStr,
    GtStr,
    GeStr,
    NotBool,

    // Text
    ConcatStr,

    // Control flow
    Jump,
    BranchTrue,
    Call,
    CallBuiltin,
    Return,
    ReturnVoid,

    // Aggregates. Structs, objects, and enums are all boxed in the VM: a slot
    // holds a pointer to a heap block of slots. Uniformity is worth more here
    // than the layout savings, since this backend optimizes for compile speed.
    MakeStruct,
    GetField,
    SetField,
    MakeEnum,
    EnumTag,
    EnumPayload,

    // Lists and slices
    MakeList,
    GetIndex,
    GetSliceIndex,
    SetIndex,

    // Pointers
    Deref,
    StoreDeref,

    // Async
    Spawn,
    Await,
};

const char *op_mnemonic(Op op);

/// How a print builtin should format its argument. The compiler resolves this
/// from the argument's static type and stores it in the instruction's `b`
/// field, so the interpreter never has to inspect a value's type.
constexpr u32 kFormatInt = 0;
constexpr u32 kFormatFloat = 1;
constexpr u32 kFormatBool = 2;
constexpr u32 kFormatStr = 3;

/// One decoded instruction. Fixed width, so the interpreter indexes rather
/// than decodes: this trades a little memory for a much simpler hot loop.
struct Instr {
    Op op = Op::Halt;
    u32 dest = 0xFFFFFFFFu;
    u32 a = 0xFFFFFFFFu;
    u32 b = 0xFFFFFFFFu;
    u32 c = 0xFFFFFFFFu;
    /// Immediate payload: constant value, jump target, callee index, field
    /// index, or builtin id, depending on the opcode.
    i64 imm = 0;
    double fimm = 0.0;
    /// Call arguments live in the program's flat argument pool.
    u32 arg_offset = 0;
    u32 arg_count = 0;
    /// Source position, kept so a runtime panic can name a line.
    u32 line = 0;
};

/// Copy shape of one aggregate declaration.
///
/// Only value structs are copied: an `object` has identity, and an enum has no
/// in-place mutation, so sharing their blocks is unobservable.
struct StructLayout {
    u32 slot_count = 0;
    bool copy_by_value = false;
    /// (field index, declaration index) for fields that are themselves value
    /// structs and therefore need copying in turn.
    std::vector<std::pair<u32, u32>> nested;
};

struct BytecodeFunction {
    std::string name;
    u32 entry = 0;        // first instruction index
    u32 instruction_count = 0;
    u32 param_count = 0;
    u32 local_count = 0;
    u32 register_count = 0;
    /// Total slots a frame needs: locals followed by registers.
    u32 frame_size = 0;
    bool returns_value = false;
    bool is_async = false;
    /// Index into the runtime's native table, for `extern native fn`.
    bool is_extern_native = false;
    std::string native_symbol;
    /// (slot, declaration index) for locals holding a value struct. Each gets a
    /// zeroed block on frame entry, matching the `= {0}` the C backend emits.
    std::vector<std::pair<u32, u32>> struct_locals;
};

/// A whole compiled program: one flat instruction array plus side tables.
struct BytecodeProgram {
    std::vector<Instr> code;
    std::vector<BytecodeFunction> functions;
    /// Deduplicated string constants; ConstStr's `imm` indexes this.
    std::vector<std::string> strings;
    /// Flat pool of call argument registers.
    std::vector<u32> arguments;
    /// Copy shapes, indexed by aggregate declaration index.
    std::vector<StructLayout> layouts;
    u32 entry = 0xFFFFFFFFu;
    std::string source_name;

    /// Human-readable listing, used by `ppc emit-bytecode`.
    std::string disassemble() const;
    /// Compact binary form written by `ppc build --backend=bytecode`.
    std::string serialize() const;
    /// Reads back `serialize` output. Returns false on a malformed image.
    static bool deserialize(const std::string &bytes, BytecodeProgram &out,
                            std::string &error);
};

}  // namespace ppc

#endif
