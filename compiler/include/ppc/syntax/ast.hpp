#ifndef PPC_SYNTAX_AST_HPP
#define PPC_SYNTAX_AST_HPP

#include <string>
#include <vector>

#include "ppc/support/arena.hpp"
#include "ppc/support/common.hpp"

namespace ppc {

struct TypeExpr;
struct Expr;
struct Stmt;
struct Pattern;

// ---------------------------------------------------------------------------
// Syntactic types
// ---------------------------------------------------------------------------

/// A type as written in source. Name resolution turns this into a sema::Type;
/// keeping the two apart means a type error can point at the exact spelling the
/// author used, including the generic arguments.
struct TypeExpr {
    enum class Kind {
        Named,      // int, Player, Option<int>
        Reference,  // &T
        MutRef,     // &mut T
        RawPointer, // *T
        Infer,      // omitted annotation
        SelfType,   // Self inside an impl body
    } kind = Kind::Infer;

    Span span;
    Symbol name;                      // Named only
    std::vector<TypeExpr *> arguments;  // generic arguments, Named only
    TypeExpr *element = nullptr;        // Reference / MutRef / RawPointer

    /// Source spelling, rebuilt for diagnostics.
    std::string describe(const Interner &interner) const;
};

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

struct Pattern {
    enum class Kind {
        Wildcard,  // _
        Binding,   // name
        Variant,   // Enum::Variant(sub, sub)
        Int,
        String,
        Bool,
    } kind = Kind::Wildcard;

    Span span;
    Symbol name;          // Binding name, or variant name
    Symbol enum_name;     // qualifier for Variant, if written
    std::vector<Pattern *> children;
    i64 int_value = 0;
    Symbol string_value;
    bool bool_value = false;

    // Filled in by the checker.
    u32 variant_index = 0;
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class UnaryOp { Negate, Not, BitNot, Deref, Ref, MutRef, RawRef, Await, Move, Drop };

enum class BinaryOp {
    Add, Subtract, Multiply, Divide, Modulo,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or,
    BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
};

const char *binary_op_spelling(BinaryOp op);
const char *unary_op_spelling(UnaryOp op);
/// True for operators that yield bool regardless of operand type.
bool is_comparison(BinaryOp op);

struct MatchArm {
    Pattern *pattern = nullptr;
    Expr *guard = nullptr;  // reserved; guards do not count toward exhaustiveness
    Expr *value = nullptr;
    std::vector<Stmt *> body;  // used when the arm is a block
    bool has_block = false;
    Span span;
};

struct Argument {
    Symbol name;  // invalid for positional arguments
    Expr *value = nullptr;
    Span span;
};

struct Expr {
    enum class Kind {
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        BoolLiteral,
        ListLiteral,
        Name,
        SelfExpr,
        Path,        // Enum::Variant, with optional payload via Call
        Call,
        MethodCall,
        Field,
        Index,
        Unary,
        Binary,
        Match,
        Propagate,   // postfix ?
        SizeOf,
        AlignOf,
        Range,       // a..b, only valid as a `for` iterable
        Assign,      // assignment used in expression position (desugared)
    } kind = Kind::IntLiteral;

    Span span;

    // Literals
    i64 int_value = 0;
    double float_value = 0.0;
    Symbol string_value;
    bool bool_value = false;

    // Names and paths
    Symbol name;
    Symbol qualifier;  // `Option` in `Option::Some`
    std::vector<TypeExpr *> type_arguments;

    // Structure
    Expr *left = nullptr;    // receiver, operand, scrutinee, callee, base
    Expr *right = nullptr;   // rhs, index, range end
    std::vector<Argument> arguments;
    std::vector<Expr *> elements;  // list literal
    std::vector<MatchArm> arms;
    TypeExpr *type_operand = nullptr;  // sizeof / alignof

    UnaryOp unary_op = UnaryOp::Negate;
    BinaryOp binary_op = BinaryOp::Add;

    /// Set by the ownership pass when this expression transfers a named
    /// move-only binding, so backends know to clear the drop flag.
    bool consumes = false;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

struct Stmt {
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
        Unsafe,
    } kind = Kind::Expression;

    Span span;

    // Let
    Symbol name;
    TypeExpr *declared_type = nullptr;
    bool is_mutable = false;
    bool is_const = false;

    // Assign: target op= value, where op is Equal for plain assignment.
    Expr *target = nullptr;
    BinaryOp compound_op = BinaryOp::Add;
    bool is_compound = false;

    // Shared payload: initializer, condition, returned value, expression.
    Expr *value = nullptr;

    // For: `for name in start..end` and `each name from start until end`.
    Expr *range_start = nullptr;
    Expr *range_end = nullptr;

    std::vector<Stmt *> body;
    std::vector<Stmt *> alternative;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

enum class Visibility { Public, Private, Protected };

struct GenericParam {
    Symbol name;
    Span span;
    std::vector<TypeExpr *> constraints;  // T: Copy + Comparable<T>
};

struct Param {
    Symbol name;
    Span span;
    TypeExpr *type = nullptr;
    Expr *default_value = nullptr;
    bool is_mutable = false;
    Visibility visibility = Visibility::Public;  // meaningful for fields
};

struct FunctionDecl {
    Symbol name;         // source spelling
    Symbol owner;        // enclosing type for methods, invalid otherwise
    Span span;
    std::vector<GenericParam> generics;
    std::vector<Param> params;
    TypeExpr *result = nullptr;
    std::vector<Stmt *> body;

    bool is_method = false;
    bool is_initializer = false;
    bool is_async = false;
    bool is_extern_native = false;
    bool self_mutable = false;
    bool has_explicit_self = false;
    bool is_entry = false;  // `launch` block or `fn main`
    Visibility visibility = Visibility::Public;
    Symbol native_symbol;

    /// Fully qualified lowered name: `Player::hit` for methods, plain otherwise.
    std::string qualified_name(const Interner &interner) const;
};

struct EnumVariantDecl {
    Symbol name;
    Span span;
    std::vector<TypeExpr *> payload;
};

struct EnumDecl {
    Symbol name;
    Span span;
    std::vector<GenericParam> generics;
    std::vector<EnumVariantDecl> variants;
};

struct ShapeDecl {
    Symbol name;
    Span span;
    std::vector<GenericParam> generics;
    std::vector<Param> fields;
    std::vector<FunctionDecl *> methods;
    FunctionDecl *initializer = nullptr;
    std::vector<Symbol> contracts;
    /// `object` has identity/reference semantics; `struct` has value semantics.
    bool is_reference = false;
    bool is_sealed = false;
};

struct ContractMethodDecl {
    Symbol name;
    Span span;
    std::vector<Param> params;
    TypeExpr *result = nullptr;
};

struct ContractDecl {
    Symbol name;
    Span span;
    std::vector<GenericParam> generics;
    std::vector<ContractMethodDecl> methods;
};

struct ImportDecl {
    std::vector<Symbol> segments;  // std, math
    Span span;
};

struct InjectionDecl {
    Symbol language;  // c, cpp, rust, asm
    Symbol source;
    Span span;
};

/// One parsed file.
struct Module {
    FileId file;
    std::string path;
    std::vector<ImportDecl> imports;
    std::vector<FunctionDecl *> functions;
    std::vector<ShapeDecl *> shapes;
    std::vector<EnumDecl *> enums;
    std::vector<ContractDecl *> contracts;
    std::vector<InjectionDecl> injections;
};

/// Every module in one compilation, in dependency order (imports first).
struct Program {
    std::vector<Module *> modules;
};

}  // namespace ppc

#endif
