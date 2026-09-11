#ifndef PPC_SEMA_CHECKER_HPP
#define PPC_SEMA_CHECKER_HPP

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ppc/hir/hir.hpp"
#include "ppc/sema/builtins.hpp"
#include "ppc/sema/type.hpp"
#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

/// Maps generic parameter names to the concrete types bound at a call site.
using Substitution = std::map<u32, const Type *>;

/// A generic or concrete function as written in source, before specialization.
struct FunctionTemplate {
    const FunctionDecl *decl = nullptr;
    Symbol name;
    Symbol owner;  // enclosing type for methods
    std::vector<Symbol> generics;
    /// Parameter/result types resolved with generic parameters left abstract.
    /// Used for overload pre-filtering without committing to a specialization.
    std::vector<const Type *> rough_params;
    const Type *rough_result = nullptr;
    bool is_entry = false;
};

/// One concrete instantiation of a template. Non-generic functions get exactly
/// one of these; generic functions get one per distinct type-argument tuple.
struct Specialization {
    u32 templ = 0;
    std::vector<const Type *> arguments;
    std::vector<const Type *> params;
    const Type *result = nullptr;
    std::string mangled;
    HirFunction *hir = nullptr;
    bool checked = false;
};

/// A local variable in the function currently being checked.
struct LocalBinding {
    Symbol name;
    u32 slot = 0;
    const Type *type = nullptr;
    bool is_mutable = false;
    /// Move tracking, used by the ownership rules in spec/0.6/ownership.md.
    bool moved = false;
    bool maybe_moved = false;
    Span move_span;
    /// Number of live borrows taken of this binding, split by strength.
    int shared_borrows = 0;
    int mutable_borrows = 0;
    /// Where the most recent live borrow was taken, for conflict diagnostics.
    Span borrow_span;
    /// When this binding is itself a borrow, the slot it borrows from. The
    /// borrow is released when this binding leaves scope, which is what makes
    /// borrow lifetimes lexical.
    u32 borrows_from = 0xFFFFFFFFu;
};

/// Resolves names and types across a whole program and produces typed HIR.
///
/// The pipeline runs in three passes so declarations can refer to each other in
/// any order:
///   1. collect  — register every type and function name
///   2. resolve  — fill in field, variant, parameter, and result types
///   3. check    — walk function bodies, emitting HIR and diagnostics
///
/// Generic functions and types are monomorphized on demand during pass 3; each
/// distinct type-argument tuple is checked exactly once and cached.
class Checker {
  public:
    Checker(TypeContext &types, Arena &arena, Interner &interner, DiagnosticEngine &diagnostics)
        : types_(types), arena_(arena), interner_(interner), diagnostics_(diagnostics) {}

    /// Runs all three passes. Returns nullptr when errors make HIR meaningless.
    HirProgram *check(const Program &program);

  private:
    // -- passes -------------------------------------------------------------
    void collect(const Program &program);
    void resolve_signatures();
    void check_bodies();

    // -- types --------------------------------------------------------------
    const Type *resolve_type(const TypeExpr *expr, const Substitution &subst);
    const Type *substitute(const Type *type, const Substitution &subst);
    /// Instantiates a generic struct/object/enum for concrete arguments,
    /// creating and caching the specialized declaration on first use.
    const Type *instantiate_named(Symbol name, std::vector<const Type *> arguments, Span span);
    void register_prelude();

    /// Detects a type that contains itself without indirection.
    ///
    /// `enum Expr { Add(Expr, Expr) }` has no finite layout: each Expr would
    /// have to embed two more. Backends differ in how close they come to
    /// supporting it — the boxed ones could, the C backend cannot — so it is
    /// rejected here, in the front end, to give every backend the same answer.
    bool has_value_cycle(const Type *type, std::vector<const Type *> &visiting) const;
    void reject_recursive_type(const Type *type, Symbol name, Span span);

    // -- functions ----------------------------------------------------------
    /// Finds or creates the specialization for `templ` with `arguments`, queues
    /// it for checking, and returns its index.
    u32 specialize(u32 templ, std::vector<const Type *> arguments, Span span);
    void check_function(u32 specialization);
    Specialization &spec_at(u32 index) { return *specializations_[index]; }
    /// Infers type arguments for a generic call from the argument types, then
    /// from the expected result type.
    bool infer_arguments(const FunctionTemplate &templ, const std::vector<const Type *> &actual,
                         const std::vector<const Type *> &explicit_args, const Type *expected,
                         std::vector<const Type *> &out, Span span);
    bool unify(const Type *pattern, const Type *actual, Substitution &subst);
    bool satisfies_constraints(const FunctionTemplate &templ,
                               const std::vector<const Type *> &arguments, Span span);
    bool type_meets_contract(const Type *type, Symbol contract) const;

    // -- statements ---------------------------------------------------------
    void check_block(const std::vector<Stmt *> &body, std::vector<HirStmt *> &out);
    HirStmt *check_statement(const Stmt *statement);
    HirStmt *check_let(const Stmt *statement);
    HirStmt *check_assign(const Stmt *statement);
    HirStmt *check_if(const Stmt *statement);
    HirStmt *check_while(const Stmt *statement);
    HirStmt *check_for(const Stmt *statement);
    HirStmt *check_for_each(const Stmt *statement);
    HirStmt *check_return(const Stmt *statement);

    // -- expressions --------------------------------------------------------
    /// `expected` is a hint used for inference; a null hint means no context.
    HirExpr *check_expr(const Expr *expr, const Type *expected = nullptr);
    HirExpr *check_call(const Expr *expr, const Type *expected);
    HirExpr *check_method_call(const Expr *expr, const Type *expected);
    HirExpr *check_binary(const Expr *expr);
    HirExpr *check_unary(const Expr *expr, const Type *expected);
    HirExpr *check_name(const Expr *expr, const Type *expected);
    HirExpr *check_path(const Expr *expr, const Type *expected);
    HirExpr *check_field(const Expr *expr);
    HirExpr *check_index(const Expr *expr);
    HirExpr *check_match(const Expr *expr, const Type *expected);
    HirExpr *check_propagate(const Expr *expr);
    HirExpr *check_builtin_call(const Expr *expr, BuiltinId builtin,
                                const Type *expected = nullptr);
    HirExpr *check_constructor(const Expr *expr, const Type *type, u32 struct_index);
    HirExpr *check_enum_construct(const Expr *expr, const Type *enum_type, u32 variant,
                                  const std::vector<const Expr *> &payload);

    /// Saves and restores everything a speculative check could disturb.
    ///
    /// Overload resolution and generic inference need to know an argument's
    /// type before the parameter type is known, so arguments get checked once
    /// to probe and again for real. Without this guard the probe would apply
    /// its effects twice: a `move(x)` argument would mark `x` moved during the
    /// probe and then report a spurious use-after-move during the real check.
    class SpeculativeScope {
      public:
        explicit SpeculativeScope(Checker &checker)
            : checker_(checker), diagnostics_(checker.diagnostics_.mark()),
              scopes_(checker.scopes_),
              locals_(checker.function_ ? checker.function_->locals.size() : 0) {}

        ~SpeculativeScope() {
            checker_.diagnostics_.rewind(diagnostics_);
            checker_.scopes_ = std::move(scopes_);
            // Locals introduced by the probe are unreachable afterwards, so the
            // slots are handed back rather than left as dead entries.
            if (checker_.function_) checker_.function_->locals.resize(locals_);
        }

        SpeculativeScope(const SpeculativeScope &) = delete;
        SpeculativeScope &operator=(const SpeculativeScope &) = delete;

      private:
        Checker &checker_;
        std::size_t diagnostics_;
        std::vector<std::vector<LocalBinding>> scopes_;
        std::size_t locals_;
    };

    /// Types of each positional argument, checked with no context and with every
    /// effect discarded. Named arguments yield nullptr because they are matched
    /// by name rather than position.
    std::vector<const Type *> probe_argument_types(const std::vector<Argument> &arguments);

    /// Binds call arguments to parameters, applying named arguments and
    /// defaults. Returns false when the call is not viable.
    bool bind_arguments(const std::vector<Argument> &arguments,
                        const std::vector<Param> &params,
                        const std::vector<const Type *> &param_types, Span span,
                        std::vector<HirExpr *> &out, bool report);

    // -- patterns -----------------------------------------------------------
    /// Lowers one arm's pattern into a tag test plus payload bindings.
    bool lower_pattern(const Pattern *pattern, const Type *scrutinee, HirArm &arm,
                       std::vector<u32> &introduced);

    /// One row of the pattern matrix used for exhaustiveness and reachability.
    using PatternRow = std::vector<const Pattern *>;
    using PatternMatrix = std::vector<PatternRow>;

    /// Searches for a value that no row of `matrix` matches, using Maranget's
    /// usefulness algorithm. Returns true and fills `witness` with a rendering
    /// of an uncovered value when one exists.
    ///
    /// Tag counting is not enough here: `Item(Some(v))` and `Item(None)` both
    /// match the same variant, and only together do they cover it. The matrix
    /// formulation handles nesting to any depth.
    bool find_missing_value(const PatternMatrix &matrix,
                            const std::vector<const Type *> &columns, std::string &witness);

    /// Payload types introduced by specializing on `pattern`'s constructor.
    std::vector<const Type *> constructor_fields(const Pattern *pattern, const Type *column);

    void check_exhaustiveness(const Expr *expr, const Type *scrutinee,
                              const std::vector<const Pattern *> &patterns);

    // -- scopes and locals --------------------------------------------------
    void push_scope();
    void pop_scope();
    u32 declare_local(Symbol name, const Type *type, bool is_mutable, bool is_parameter, Span span);
    LocalBinding *lookup_local(Symbol name);

    // -- ownership ----------------------------------------------------------
    /// Records that `expr` moved out of a named binding, if it did.
    void note_move(const Expr *source, HirExpr *lowered);
    void reject_if_moved(Symbol name, Span span);

    // -- diagnostics helpers ------------------------------------------------
    HirExpr *poison(Span span);
    HirExpr *make_expr(HirExpr::Kind kind, Span span, const Type *type);
    HirStmt *make_stmt(HirStmt::Kind kind, Span span);
    void expect_type(const Type *actual, const Type *expected, Span span, const char *context);

    TypeContext &types_;
    Arena &arena_;
    Interner &interner_;
    DiagnosticEngine &diagnostics_;

    // Global tables.
    std::vector<FunctionTemplate> templates_;
    std::unordered_map<std::string, std::vector<u32>> by_name_;  // overload sets
    /// Boxed for the same reason as the type tables: a Specialization is read
    /// across calls that can create more specializations.
    std::vector<std::unique_ptr<Specialization>> specializations_;
    std::unordered_map<std::string, u32> specialization_index_;
    std::vector<u32> worklist_;

    /// Named type declarations, keyed by source name. Generic templates and
    /// their instantiations share an entry; instantiations are cached in
    /// `instantiations_`.
    std::unordered_map<std::string, const ShapeDecl *> shape_decls_;
    std::unordered_map<std::string, const EnumDecl *> enum_decls_;
    std::unordered_map<std::string, u32> contract_index_;
    std::unordered_map<std::string, const Type *> instantiations_;

    // Per-function state.
    HirFunction *function_ = nullptr;
    const Specialization *current_ = nullptr;
    Substitution subst_;
    std::vector<std::vector<LocalBinding>> scopes_;
    const Type *result_type_ = nullptr;
    /// Generic parameters known to satisfy `Copy` in the current specialization.
    std::vector<Symbol> copy_params_;
    int loop_depth_ = 0;
    int unsafe_depth_ = 0;
    bool in_async_ = false;
    /// Guards against runaway recursive specialization.
    int specialization_depth_ = 0;
    static constexpr int kMaxSpecializationDepth = 64;

    HirProgram *program_ = nullptr;
};

}  // namespace ppc

#endif
