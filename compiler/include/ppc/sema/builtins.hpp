#ifndef PPC_SEMA_BUILTINS_HPP
#define PPC_SEMA_BUILTINS_HPP

#include <string>
#include <vector>

#include "ppc/hir/hir.hpp"
#include "ppc/sema/type.hpp"

namespace ppc {

/// How a builtin's parameter or result type is described before types exist.
/// A few builtins are polymorphic in ways the ordinary type rules cannot state,
/// so they get a marker the checker special-cases.
enum class BuiltinType {
    Void,
    Bool,
    Int,
    Float,
    Str,
    Nums,
    IntSlice,
    /// Accepts any scalar; `print`/`println`/`say` dispatch on the argument's
    /// runtime representation.
    AnyScalar,
    /// Result equals the argument type, as for `move`.
    SameAsArgument,
    /// Any task; the result is the task's payload type, as for `await`.
    AnyTask,
    /// Any List<T>. Used for the first parameter of the list builtins.
    AnyList,
    /// The element type of whichever argument was AnyList.
    ///
    /// This is what makes one runtime implementation serve every List<T>:
    /// `list_at` returns T, and T is read from the list the call was given
    /// rather than from a separate type argument.
    ListElement,
    /// A List whose element type comes from the expected type at the call site,
    /// or from an explicit type argument. Used for `list<T>()`.
    NewList,
    /// Any Map<V>. Used for the first parameter of the map builtins.
    AnyMap,
    /// The value type of whichever argument was AnyMap.
    MapValue,
    /// A Map whose value type comes from context or an explicit type argument.
    NewMap,
    /// A List<str>, for builtins that return a sequence of names.
    ListOfStr,
    /// The bytes buffer type.
    Bytes,
};

struct BuiltinSpec {
    const char *name;
    std::vector<BuiltinType> params;
    BuiltinType result;
    /// Symbol in the runtime library, or empty when the builtin is expanded
    /// inline by the backends (`move`, `drop`, `print` families).
    const char *symbol;
    const char *documentation;
};

/// The complete builtin table, in a stable order. The index into this table is
/// the BuiltinId stored in HirExpr.
const std::vector<BuiltinSpec> &builtin_table();

/// Index of `name` in the builtin table, or kNotBuiltin.
BuiltinId find_builtin(const std::string &name);

/// Concrete type for a BuiltinType marker, or nullptr for the polymorphic ones.
const Type *builtin_type_to_type(BuiltinType kind, TypeContext &types);

/// Rendered signature, used in arity and type diagnostics.
std::string builtin_signature(const BuiltinSpec &spec);

}  // namespace ppc

#endif
