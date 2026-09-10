#ifndef PPC_SEMA_TYPE_HPP
#define PPC_SEMA_TYPE_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "ppc/support/common.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

enum class TypeKind {
    Error,    // poison; suppresses cascading diagnostics
    Void,
    Bool,
    Int,      // i64
    Float,    // f64
    Str,
    Nums,     // shared-handle integer list
    Struct,   // value semantics
    Object,   // identity / reference semantics
    Enum,
    Reference,   // &T
    MutRef,      // &mut T
    RawPointer,  // *T
    Slice,       // Slice<T>, a non-owning borrowed view
    /// List<T>, a growable owning sequence of any element type.
    ///
    /// `nums` predates this and remains a distinct type for source
    /// compatibility: it is specifically a list of int with its own builtin
    /// surface. List<T> is the general form, and the one a program should reach
    /// for when the element is anything else.
    List,
    /// Map<V>, a string-keyed hash map. The key is always str, so only the
    /// value type varies; that covers symbol tables, headers, config, and JSON
    /// objects, which is nearly everything a map is used for.
    Map,
    /// bytes, a mutable byte buffer. Distinct from str because str is UTF-8
    /// text with no NUL, and binary data is neither.
    Bytes,
    Task,        // @task:T
    Param,       // an unsubstituted generic parameter
};

struct Type;

/// Interned, immutable type. Two structurally identical types are always the
/// same pointer, so type equality is a pointer comparison.
struct Type {
    TypeKind kind = TypeKind::Error;
    Symbol name;                          // named types and generic parameters
    const Type *element = nullptr;        // Reference / MutRef / RawPointer / Slice / Task
    std::vector<const Type *> arguments;  // generic arguments of a named type
    /// Index into TypeContext's struct/enum tables. Only meaningful for
    /// Struct, Object, and Enum.
    u32 decl = 0xFFFFFFFFu;

    bool is_error() const { return kind == TypeKind::Error; }
    bool is_numeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }
    bool is_aggregate() const {
        return kind == TypeKind::Struct || kind == TypeKind::Object || kind == TypeKind::Enum;
    }
    bool is_pointer_like() const {
        return kind == TypeKind::Reference || kind == TypeKind::MutRef ||
               kind == TypeKind::RawPointer;
    }
    /// True for types that live in one machine word and copy freely.
    bool is_scalar() const {
        switch (kind) {
            case TypeKind::Bool:
            case TypeKind::Int:
            case TypeKind::Float:
            case TypeKind::Str:
            case TypeKind::Nums:
            case TypeKind::Reference:
            case TypeKind::MutRef:
            case TypeKind::RawPointer:
            case TypeKind::Slice:
            case TypeKind::List:
            case TypeKind::Map:
            case TypeKind::Bytes:
            case TypeKind::Task:
            case TypeKind::Object:
                return true;
            default:
                return false;
        }
    }
};

struct FieldInfo {
    Symbol name;
    const Type *type = nullptr;
    Visibility visibility = Visibility::Public;
    bool is_mutable = false;
    Span span;
};

struct StructInfo {
    Symbol name;
    Span span;
    bool is_reference = false;  // `object` rather than `struct`
    bool is_sealed = false;
    std::vector<FieldInfo> fields;
    std::vector<Symbol> contracts;
    /// Mangled names of this type's methods, for lookup in the function table.
    std::vector<std::string> methods;
    std::string initializer;
    /// Generic parameter names, empty for a concrete type. A generic template
    /// is never lowered directly; only its specializations are.
    std::vector<Symbol> generics;
    /// Concrete arguments when this entry is a specialization.
    std::vector<const Type *> arguments;
    const ShapeDecl *decl = nullptr;
};

struct VariantInfo {
    Symbol name;
    std::vector<const Type *> payload;
    Span span;
};

struct EnumInfo {
    Symbol name;
    Span span;
    std::vector<VariantInfo> variants;
    std::vector<Symbol> generics;
    std::vector<const Type *> arguments;
    const EnumDecl *decl = nullptr;
};

struct ContractInfo {
    Symbol name;
    Span span;
    std::vector<Symbol> methods;
    const ContractDecl *decl = nullptr;
};

/// Owns and interns every Type in a compilation.
class TypeContext {
  public:
    explicit TypeContext(Interner &interner);

    // Primitives, allocated once at construction.
    const Type *error() const { return error_; }
    const Type *void_type() const { return void_; }
    const Type *bool_type() const { return bool_; }
    const Type *int_type() const { return int_; }
    const Type *float_type() const { return float_; }
    const Type *str_type() const { return str_; }
    const Type *nums_type() const { return nums_; }

    const Type *reference(const Type *element, bool mutable_ref);
    const Type *raw_pointer(const Type *element);
    const Type *slice(const Type *element);
    const Type *list(const Type *element);
    const Type *map(const Type *value);
    const Type *bytes_type() const { return bytes_; }
    const Type *task(const Type *result);
    const Type *param(Symbol name);
    const Type *named(TypeKind kind, Symbol name, std::vector<const Type *> arguments, u32 decl);

    /// Human-readable spelling used in diagnostics.
    std::string describe(const Type *type) const;
    /// Stable mangling used for specialization keys and backend symbols.
    std::string mangle(const Type *type) const;

    // Declaration tables. Indices are stable for the life of the context.
    u32 add_struct(StructInfo info);
    u32 add_enum(EnumInfo info);
    u32 add_contract(ContractInfo info);

    // Declaration info is handed out by reference and read across calls that
    // can instantiate further generics, so the storage must not move. Boxing
    // each entry makes those references stable by construction rather than
    // relying on every call site to copy defensively.
    StructInfo &struct_at(u32 index) { return *structs_[index]; }
    const StructInfo &struct_at(u32 index) const { return *structs_[index]; }
    EnumInfo &enum_at(u32 index) { return *enums_[index]; }
    const EnumInfo &enum_at(u32 index) const { return *enums_[index]; }
    ContractInfo &contract_at(u32 index) { return *contracts_[index]; }
    const ContractInfo &contract_at(u32 index) const { return *contracts_[index]; }

    std::size_t struct_count() const { return structs_.size(); }
    std::size_t enum_count() const { return enums_.size(); }
    std::size_t contract_count() const { return contracts_.size(); }

    Interner &interner() { return interner_; }
    const Interner &interner() const { return interner_; }

    /// True when a value of `from` may be used where `to` is expected. PunPun
    /// has no implicit numeric or text conversions, so this is equality plus a
    /// small set of reference relaxations.
    bool assignable(const Type *from, const Type *to) const;

    /// True when the type copies freely and needs no destruction, which is what
    /// the built-in `Copy` constraint requires.
    bool is_copy(const Type *type) const;

    /// True when the type owns a resource that must be released at scope exit.
    bool needs_drop(const Type *type) const;

  private:
    /// Both predicates walk a type's members, and an enum may reach itself:
    /// `enum Expr { Add(Expr, Expr) }` is legal because enum payloads are boxed,
    /// so the recursion is through a pointer and the type has finite size.
    /// Without a visited set these walks do not terminate.
    ///
    /// Revisiting a type is treated as contributing nothing: the question in
    /// both cases is whether *some* member breaks the property, and a cycle back
    /// to a type already under consideration adds no new member to check. This
    /// is the standard coinductive reading and it gives the right answer for
    /// both predicates.
    bool is_copy_impl(const Type *type, std::vector<const Type *> &visiting) const;
    bool needs_drop_impl(const Type *type, std::vector<const Type *> &visiting) const;

  public:

  private:
    const Type *intern(Type &&candidate);

    Interner &interner_;
    std::vector<std::unique_ptr<Type>> storage_;
    std::unordered_map<std::string, const Type *> cache_;

    std::vector<std::unique_ptr<StructInfo>> structs_;
    std::vector<std::unique_ptr<EnumInfo>> enums_;
    std::vector<std::unique_ptr<ContractInfo>> contracts_;

    const Type *error_ = nullptr;
    const Type *void_ = nullptr;
    const Type *bool_ = nullptr;
    const Type *int_ = nullptr;
    const Type *float_ = nullptr;
    const Type *str_ = nullptr;
    const Type *nums_ = nullptr;
    const Type *bytes_ = nullptr;
};

}  // namespace ppc

#endif
