#ifndef PPC_HIR_HIR_HPP
#define PPC_HIR_HIR_HPP

#include <string>
#include <vector>

#include "ppc/sema/type.hpp"
#include "ppc/support/arena.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

struct HirExpr;
struct HirStmt;

/// Identifies one of the compiler's built-in functions. The table in
/// builtins.cpp maps each to its signature and runtime symbol.
using BuiltinId = u16;
constexpr BuiltinId kNotBuiltin = 0xFFFF;

/// Typed, fully resolved expression.
///
/// Everything ambiguous in the AST is settled here: names point at local slots
/// or function indices, generic calls name a concrete specialization, operators
/// know their operand types, and enum construction carries a variant index.
/// Nothing downstream needs the symbol table.
struct HirExpr {
    enum class Kind {
        ConstInt,
        ConstFloat,
        ConstStr,
        ConstBool,
        Local,        // read a local slot
        /// The value being matched, inside an arm's test expression. The MIR
        /// builder substitutes the scrutinee register; it never reaches a
        /// backend.
        MatchSubject,
        Call,         // direct call to a known function index
        /// A function used as a value. Carries the specialization index in
        /// `target`; the value itself is that index, which is why a function
        /// value needs no representation beyond an integer.
        FuncRef,
        /// Call through a value of function type. `left` is the callee.
        CallIndirect,
        CallBuiltin,  // call to a runtime builtin
        Field,        // struct/object field read
        Index,        // nums element read
        Unary,
        Binary,
        MakeStruct,
        MakeEnum,
        EnumTag,      // discriminant of an enum value
        EnumPayload,  // payload slot of a known variant
        ListLiteral,
        Match,
        Ref,          // &place / &mut place
        Deref,        // *pointer
        Await,
        /// A block that evaluates statements then yields `value`. Produced when
        /// a match arm needs statements, and by `?` desugaring.
        Block,
    } kind = Kind::ConstInt;

    Span span;
    const Type *type = nullptr;

    i64 int_value = 0;
    double float_value = 0.0;
    Symbol string_value;
    bool bool_value = false;

    u32 local = 0;      // Local
    u32 target = 0;     // Call: function index; MakeStruct/MakeEnum: decl index
    u32 field = 0;      // Field: field index; MakeEnum/EnumPayload: variant index
    u32 slot = 0;       // EnumPayload: which payload element
    BuiltinId builtin = kNotBuiltin;

    UnaryOp unary_op = UnaryOp::Negate;
    BinaryOp binary_op = BinaryOp::Add;
    bool mutable_ref = false;
    /// Set when this expression moves a move-only value out of a binding.
    bool consumes = false;

    HirExpr *left = nullptr;
    HirExpr *right = nullptr;
    std::vector<HirExpr *> operands;   // call arguments, struct fields, list elements
    std::vector<HirStmt *> body;       // Block
    std::vector<struct HirArm> arms;   // Match
};

/// Where one pattern binding gets its value from.
///
/// A binding either takes the scrutinee whole, or projects one payload slot out
/// of a known variant of some already-bound value. Nested patterns chain through
/// `source`: the inner binding reads a temporary the outer one produced, which
/// is what lets `Nested::Item(Option::Some(v))` bind `v` in two hops.
struct HirBinding {
    /// Local slot the value is stored into.
    u32 local = 0;
    /// Local slot to project from, or kScrutinee for the match subject itself.
    u32 source = kScrutinee;
    /// Variant to project out of; ignored when taking the whole value.
    u32 variant = 0;
    /// Payload index inside that variant, or kWholeValue to bind `source` as is.
    u32 payload_slot = kWholeValue;

    static constexpr u32 kScrutinee = 0xFFFFFFFFu;
    static constexpr u32 kWholeValue = 0xFFFFFFFFu;
};

/// One step in matching an arm: either a condition that must hold, or a
/// binding to establish.
///
/// Order is load-bearing and is why this is a sequence rather than two lists. A
/// nested pattern such as `Held(Some(v))` must test that the payload really is
/// `Some` *before* projecting `v` out of it. Extracting every binding up front
/// and testing afterwards reads a payload slot that a `None` value does not
/// have.
struct HirArmStep {
    enum class Kind { Test, Bind } kind = Kind::Bind;
    /// Kind::Test — must evaluate true for the arm to continue.
    HirExpr *test = nullptr;
    /// Kind::Bind — the value to establish.
    HirBinding binding;
};

/// One lowered match arm. The pattern has already been reduced to a
/// discriminant test plus an ordered sequence of tests and bindings, so no
/// backend needs pattern logic.
struct HirArm {
    /// Discriminant this arm matches; ignored when `is_default` is set.
    i64 tag = 0;
    bool is_default = false;
    /// Tests and bindings in the order they must be performed. A failed test
    /// abandons the arm and moves to the next one.
    std::vector<HirArmStep> steps;
    HirExpr *value = nullptr;
    std::vector<HirStmt *> body;
    Span span;
};

struct HirStmt {
    enum class Kind {
        Let,
        Assign,
        Expression,
        Return,
        If,
        While,
        For,
        Break,
        Continue,
        Block,
        Drop,  // deterministic destruction, inserted by the ownership pass
    } kind = Kind::Expression;

    Span span;

    u32 local = 0;              // Let / For induction variable / Drop
    HirExpr *value = nullptr;   // initializer, condition, returned value
    HirExpr *place = nullptr;   // Assign destination
    HirExpr *range_start = nullptr;
    HirExpr *range_end = nullptr;
    std::vector<HirStmt *> body;
    std::vector<HirStmt *> alternative;
};

struct HirLocal {
    Symbol name;
    const Type *type = nullptr;
    bool is_mutable = false;
    bool is_parameter = false;
    Span span;
};

struct HirFunction {
    /// Mangled, globally unique name. For a specialization this includes the
    /// concrete type arguments, which is what makes specializations distinct
    /// symbols in the object file.
    std::string name;
    Symbol source_name;
    Span span;
    std::vector<HirLocal> locals;  // parameters occupy the first `param_count`
    u32 param_count = 0;
    const Type *result = nullptr;
    std::vector<HirStmt *> body;

    bool is_entry = false;
    bool is_async = false;
    bool is_extern_native = false;
    std::string native_symbol;
    /// Set for functions reached from the entry point. Unreachable functions are
    /// skipped by the backends.
    bool reachable = false;
};

/// The whole program in typed form: the unit handed to the optimizer and
/// backends.
struct HirProgram {
    std::vector<HirFunction *> functions;
    /// Index into `functions` of the program entry point.
    u32 entry = 0xFFFFFFFFu;
    /// Foreign source blocks to compile and link alongside the program.
    std::vector<InjectionDecl> injections;
};

}  // namespace ppc

#endif
