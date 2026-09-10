#include "ppc/sema/checker.hpp"

#include <algorithm>
#include <functional>

namespace ppc {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

HirExpr *Checker::make_expr(HirExpr::Kind kind, Span span, const Type *type) {
    HirExpr *node = arena_.make<HirExpr>();
    node->kind = kind;
    node->span = span;
    node->type = type ? type : types_.error();
    return node;
}

HirStmt *Checker::make_stmt(HirStmt::Kind kind, Span span) {
    HirStmt *node = arena_.make<HirStmt>();
    node->kind = kind;
    node->span = span;
    return node;
}

HirExpr *Checker::poison(Span span) {
    return make_expr(HirExpr::Kind::ConstInt, span, types_.error());
}

void Checker::expect_type(const Type *actual, const Type *expected, Span span,
                          const char *context) {
    if (!actual || !expected) return;
    if (types_.assignable(actual, expected)) return;
    if (actual->is_error() || expected->is_error()) return;
    diagnostics_
        .error(Code::TypeMismatch, std::string("type mismatch in ") + context)
        .label(span, "this is " + types_.describe(actual))
        .note("expected " + types_.describe(expected))
        .with_help("PunPun has no implicit conversions; convert explicitly");
}

void Checker::push_scope() { scopes_.emplace_back(); }
void Checker::pop_scope() {
    if (!scopes_.empty()) scopes_.pop_back();
}

u32 Checker::declare_local(Symbol name, const Type *type, bool is_mutable, bool is_parameter,
                           Span span) {
    const u32 slot = static_cast<u32>(function_->locals.size());
    HirLocal local;
    local.name = name;
    local.type = type ? type : types_.error();
    local.is_mutable = is_mutable;
    local.is_parameter = is_parameter;
    local.span = span;
    function_->locals.push_back(local);

    if (scopes_.empty()) push_scope();
    LocalBinding binding;
    binding.name = name;
    binding.slot = slot;
    binding.type = local.type;
    binding.is_mutable = is_mutable;
    scopes_.back().push_back(binding);
    return slot;
}

LocalBinding *Checker::lookup_local(Symbol name) {
    // Innermost scope wins, so an inner binding shadows an outer one.
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry) {
            if (entry->name == name) return &*entry;
        }
    }
    return nullptr;
}

void Checker::reject_if_moved(Symbol name, Span span) {
    LocalBinding *binding = lookup_local(name);
    if (!binding) return;
    if (binding->moved) {
        diagnostics_
            .error(Code::UseAfterMove,
                   "use of '" + interner_.text(name) + "' after it was moved")
            .label(span, "used here after the move")
            .secondary(binding->move_span, "moved here")
            .with_help("assign a new value to the binding before using it again");
        // Clear the flag so the same binding does not report on every later use.
        binding->moved = false;
    } else if (binding->maybe_moved) {
        diagnostics_
            .error(Code::MaybeMoved,
                   "'" + interner_.text(name) + "' may have been moved on some paths")
            .label(span, "used here")
            .secondary(binding->move_span, "conditionally moved here")
            .with_help("move on all paths or none, or reinitialize before this use");
        binding->maybe_moved = false;
    }
}

void Checker::note_move(const Expr *source, HirExpr *lowered) {
    if (!source || !lowered) return;
    if (source->kind != Expr::Kind::Name) return;
    LocalBinding *binding = lookup_local(source->name);
    if (!binding) return;
    // Only move-only values leave a hole behind; `Copy` values are duplicated.
    if (types_.is_copy(binding->type)) return;
    binding->moved = true;
    binding->move_span = source->span;
    lowered->consumes = true;
}

// ---------------------------------------------------------------------------
// Type resolution
// ---------------------------------------------------------------------------

const Type *Checker::resolve_type(const TypeExpr *expr, const Substitution &subst) {
    if (!expr) return types_.error();

    switch (expr->kind) {
        case TypeExpr::Kind::Infer: return nullptr;  // caller infers
        case TypeExpr::Kind::SelfType: return types_.error();
        case TypeExpr::Kind::Reference:
            return types_.reference(resolve_type(expr->element, subst), false);
        case TypeExpr::Kind::MutRef:
            return types_.reference(resolve_type(expr->element, subst), true);
        case TypeExpr::Kind::RawPointer:
            return types_.raw_pointer(resolve_type(expr->element, subst));
        case TypeExpr::Kind::Named:
            break;
    }

    const std::string name = interner_.text(expr->name);

    // A generic parameter bound in this specialization resolves to its argument.
    auto bound = subst.find(expr->name.index);
    if (bound != subst.end() && expr->arguments.empty()) return bound->second;

    // Primitive spellings. `i64`/`int` and `f64`/`float` are the same type;
    // `str`/`text`/`String` likewise, matching the reference frontend.
    if (name == "int" || name == "i64" || name == "i32" || name == "u64" || name == "u32")
        return types_.int_type();
    if (name == "float" || name == "f64" || name == "f32") return types_.float_type();
    if (name == "bool") return types_.bool_type();
    if (name == "str" || name == "text" || name == "String") return types_.str_type();
    if (name == "nums") return types_.nums_type();
    if (name == "bytes") return types_.bytes_type();
    if (name == "void") return types_.void_type();

    std::vector<const Type *> arguments;
    arguments.reserve(expr->arguments.size());
    for (const TypeExpr *argument : expr->arguments) {
        const Type *resolved = resolve_type(argument, subst);
        arguments.push_back(resolved ? resolved : types_.error());
    }

    if (name == "Slice") {
        return types_.slice(arguments.empty() ? types_.int_type() : arguments[0]);
    }
    if (name == "Map") {
        if (arguments.size() != 1) {
            diagnostics_
                .error(Code::GenericArityMismatch,
                       "Map takes exactly one type argument, its value type")
                .label(expr->span)
                .note("keys are always str")
                .with_help("write `Map<int>`, `Map<str>`, and so on");
            return types_.error();
        }
        return types_.map(arguments[0]);
    }
    if (name == "List") {
        if (arguments.size() != 1) {
            diagnostics_
                .error(Code::GenericArityMismatch, "List takes exactly one type argument")
                .label(expr->span)
                .with_help("write `List<int>`, `List<str>`, and so on");
            return types_.error();
        }
        return types_.list(arguments[0]);
    }

    if (shape_decls_.count(name) || enum_decls_.count(name)) {
        return instantiate_named(expr->name, std::move(arguments), expr->span);
    }

    // An unbound single uppercase-ish name inside a generic context is a type
    // parameter that has not been substituted yet; keep it abstract.
    if (arguments.empty()) {
        for (const auto &entry : subst) {
            if (entry.first == expr->name.index) return entry.second;
        }
        if (current_) {
            const FunctionTemplate &templ = templates_[current_->templ];
            for (Symbol generic : templ.generics) {
                if (generic == expr->name) return types_.param(expr->name);
            }
        }
    }

    diagnostics_.error(Code::UnknownType, "unknown type '" + name + "'")
        .label(expr->span)
        .with_help("check the spelling, or import the module that defines it");
    return types_.error();
}

const Type *Checker::substitute(const Type *type, const Substitution &subst) {
    if (!type || subst.empty()) return type;
    switch (type->kind) {
        case TypeKind::Param: {
            auto it = subst.find(type->name.index);
            return it == subst.end() ? type : it->second;
        }
        case TypeKind::Reference:
            return types_.reference(substitute(type->element, subst), false);
        case TypeKind::MutRef:
            return types_.reference(substitute(type->element, subst), true);
        case TypeKind::RawPointer:
            return types_.raw_pointer(substitute(type->element, subst));
        case TypeKind::Slice:
            return types_.slice(substitute(type->element, subst));
        // A container's element type is a substitution site like any other.
        // Omitting these left `List<T>` unsubstituted in a specialization, so a
        // generic function over a list could never be called.
        case TypeKind::List:
            return types_.list(substitute(type->element, subst));
        case TypeKind::Map:
            return types_.map(substitute(type->element, subst));
        case TypeKind::Task:
            return types_.task(substitute(type->element, subst));
        case TypeKind::Struct:
        case TypeKind::Object:
        case TypeKind::Enum: {
            if (type->arguments.empty()) return type;
            std::vector<const Type *> arguments;
            arguments.reserve(type->arguments.size());
            bool changed = false;
            for (const Type *argument : type->arguments) {
                const Type *replaced = substitute(argument, subst);
                changed = changed || (replaced != argument);
                arguments.push_back(replaced);
            }
            if (!changed) return type;
            return instantiate_named(type->name, std::move(arguments), Span{});
        }
        default:
            return type;
    }
}

const Type *Checker::instantiate_named(Symbol name, std::vector<const Type *> arguments,
                                       Span span) {
    const std::string base = interner_.text(name);

    // Cache key includes the concrete arguments, so `Box<int>` and `Box<str>`
    // are distinct declarations with independently laid-out fields.
    std::string key = base;
    for (const Type *argument : arguments) {
        key += "$";
        key += types_.mangle(argument);
    }
    auto cached = instantiations_.find(key);
    if (cached != instantiations_.end()) return cached->second;

    auto shape_it = shape_decls_.find(base);
    if (shape_it != shape_decls_.end()) {
        const ShapeDecl *decl = shape_it->second;
        if (arguments.size() != decl->generics.size()) {
            diagnostics_
                .error(Code::GenericArityMismatch,
                       "type '" + base + "' takes " + std::to_string(decl->generics.size()) +
                           " type argument(s) but " + std::to_string(arguments.size()) +
                           " were given")
                .label(span);
            return types_.error();
        }

        StructInfo info;
        info.name = name;
        info.span = decl->span;
        info.is_reference = decl->is_reference;
        info.is_sealed = decl->is_sealed;
        info.contracts = decl->contracts;
        info.arguments = arguments;
        info.decl = decl;
        for (const GenericParam &generic : decl->generics) info.generics.push_back(generic.name);

        const u32 index = types_.add_struct(std::move(info));
        const Type *type = types_.named(
            decl->is_reference ? TypeKind::Object : TypeKind::Struct, name, arguments, index);
        // Register before resolving fields so a self-referential type through a
        // reference does not recurse forever.
        instantiations_.emplace(key, type);

        Substitution local;
        for (std::size_t i = 0; i < decl->generics.size(); ++i) {
            local[decl->generics[i].name.index] = arguments[i];
        }
        StructInfo &stored = types_.struct_at(index);
        for (const Param &field : decl->fields) {
            FieldInfo info_field;
            info_field.name = field.name;
            info_field.type = resolve_type(field.type, local);
            if (!info_field.type) info_field.type = types_.error();
            info_field.visibility = field.visibility;
            info_field.is_mutable = field.is_mutable;
            info_field.span = field.span;
            stored.fields.push_back(info_field);
        }
        reject_recursive_type(type, name, span);
        return type;
    }

    auto enum_it = enum_decls_.find(base);
    if (enum_it != enum_decls_.end()) {
        const EnumDecl *decl = enum_it->second;
        if (arguments.size() != decl->generics.size()) {
            diagnostics_
                .error(Code::GenericArityMismatch,
                       "enum '" + base + "' takes " + std::to_string(decl->generics.size()) +
                           " type argument(s) but " + std::to_string(arguments.size()) +
                           " were given")
                .label(span);
            return types_.error();
        }

        EnumInfo info;
        info.name = name;
        info.span = decl->span;
        info.arguments = arguments;
        info.decl = decl;
        for (const GenericParam &generic : decl->generics) info.generics.push_back(generic.name);

        const u32 index = types_.add_enum(std::move(info));
        const Type *type = types_.named(TypeKind::Enum, name, arguments, index);
        instantiations_.emplace(key, type);

        Substitution local;
        for (std::size_t i = 0; i < decl->generics.size(); ++i) {
            local[decl->generics[i].name.index] = arguments[i];
        }
        EnumInfo &stored = types_.enum_at(index);
        for (const EnumVariantDecl &variant : decl->variants) {
            VariantInfo info_variant;
            info_variant.name = variant.name;
            info_variant.span = variant.span;
            for (const TypeExpr *payload : variant.payload) {
                const Type *resolved = resolve_type(payload, local);
                info_variant.payload.push_back(resolved ? resolved : types_.error());
            }
            stored.variants.push_back(std::move(info_variant));
        }
        reject_recursive_type(type, name, span);
        return type;
    }

    diagnostics_.error(Code::UnknownType, "unknown type '" + base + "'").label(span);
    return types_.error();
}

// ---------------------------------------------------------------------------
// Prelude
// ---------------------------------------------------------------------------

bool Checker::has_value_cycle(const Type *type, std::vector<const Type *> &visiting) const {
    if (!type) return false;
    // Only value-embedded members can form an impossible layout. A reference,
    // pointer, or identity object is one word wide however deep the graph goes.
    if (type->kind != TypeKind::Struct && type->kind != TypeKind::Enum) return false;

    if (std::find(visiting.begin(), visiting.end(), type) != visiting.end()) return true;
    visiting.push_back(type);

    bool cyclic = false;
    if (type->kind == TypeKind::Struct && type->decl < types_.struct_count()) {
        for (const FieldInfo &field : types_.struct_at(type->decl).fields) {
            if (has_value_cycle(field.type, visiting)) { cyclic = true; break; }
        }
    } else if (type->kind == TypeKind::Enum && type->decl < types_.enum_count()) {
        for (const VariantInfo &variant : types_.enum_at(type->decl).variants) {
            for (const Type *payload : variant.payload) {
                if (has_value_cycle(payload, visiting)) { cyclic = true; break; }
            }
            if (cyclic) break;
        }
    }

    visiting.pop_back();
    return cyclic;
}

void Checker::reject_recursive_type(const Type *type, Symbol name, Span span) {
    std::vector<const Type *> visiting;
    if (!has_value_cycle(type, visiting)) return;

    diagnostics_
        .error(Code::FeatureUnsupported,
               "type '" + interner_.text(name) + "' contains itself without indirection")
        .label(span)
        .note("every value of this type would have to embed another one, so it has no "
              "finite size")
        .with_help("PPC does not yet support recursive types. A future release will box "
                   "the recursive payload; until then, model the recursion with a "
                   "separate index or handle.");
}

void Checker::register_prelude() {
    // Option<T> and Result<T,E> are ordinary algebraic enums, but they are
    // always in scope and `?` is defined in terms of them, so the compiler
    // synthesizes their declarations rather than requiring an import.
    static EnumDecl option;
    static EnumDecl result;
    static bool built = false;

    auto make_param = [&](const char *name) {
        GenericParam param;
        param.name = interner_.intern(name);
        return param;
    };
    auto make_named_type = [&](const char *name) {
        TypeExpr *type = arena_.make<TypeExpr>();
        type->kind = TypeExpr::Kind::Named;
        type->name = interner_.intern(name);
        return type;
    };

    if (!built) {
        built = true;
        option.name = interner_.intern("Option");
        option.generics.push_back(make_param("T"));
        {
            EnumVariantDecl none;
            none.name = interner_.intern("None");
            option.variants.push_back(none);
            EnumVariantDecl some;
            some.name = interner_.intern("Some");
            some.payload.push_back(make_named_type("T"));
            option.variants.push_back(some);
        }

        result.name = interner_.intern("Result");
        result.generics.push_back(make_param("T"));
        result.generics.push_back(make_param("E"));
        {
            EnumVariantDecl ok;
            ok.name = interner_.intern("Ok");
            ok.payload.push_back(make_named_type("T"));
            result.variants.push_back(ok);
            EnumVariantDecl error;
            error.name = interner_.intern("Error");
            error.payload.push_back(make_named_type("E"));
            result.variants.push_back(error);
        }
    }

    enum_decls_.emplace("Option", &option);
    enum_decls_.emplace("Result", &result);
}

// ---------------------------------------------------------------------------
// Pass 1: collect declarations
// ---------------------------------------------------------------------------

void Checker::collect(const Program &program) {
    register_prelude();

    for (const Module *module : program.modules) {
        for (const ShapeDecl *shape : module->shapes) {
            const std::string name = interner_.text(shape->name);
            if (shape_decls_.count(name) || enum_decls_.count(name)) {
                diagnostics_
                    .error(Code::DuplicateDefinition, "type '" + name + "' is defined twice")
                    .label(shape->span);
                continue;
            }
            shape_decls_.emplace(name, shape);
        }
        for (const EnumDecl *decl : module->enums) {
            const std::string name = interner_.text(decl->name);
            // The prelude entries are placeholders a user program may replace.
            if (name == "Option" || name == "Result") {
                enum_decls_[name] = decl;
                continue;
            }
            if (shape_decls_.count(name) || enum_decls_.count(name)) {
                diagnostics_
                    .error(Code::DuplicateDefinition, "type '" + name + "' is defined twice")
                    .label(decl->span);
                continue;
            }
            enum_decls_.emplace(name, decl);
        }
        for (const ContractDecl *decl : module->contracts) {
            ContractInfo info;
            info.name = decl->name;
            info.span = decl->span;
            info.decl = decl;
            for (const ContractMethodDecl &method : decl->methods) {
                info.methods.push_back(method.name);
            }
            const u32 index = types_.add_contract(std::move(info));
            contract_index_.emplace(interner_.text(decl->name), index);
        }
    }

    // Free functions.
    for (const Module *module : program.modules) {
        for (const FunctionDecl *fn : module->functions) {
            FunctionTemplate templ;
            templ.decl = fn;
            templ.name = fn->name;
            templ.is_entry = fn->is_entry;
            for (const GenericParam &generic : fn->generics) templ.generics.push_back(generic.name);
            templates_.push_back(std::move(templ));
            by_name_[interner_.text(fn->name)].push_back(
                static_cast<u32>(templates_.size() - 1));
        }
    }

    // Methods, registered under `Owner::method` so they never collide with a
    // free function of the same name.
    for (const auto &entry : shape_decls_) {
        const ShapeDecl *shape = entry.second;
        for (const FunctionDecl *method : shape->methods) {
            FunctionTemplate templ;
            templ.decl = method;
            templ.name = method->name;
            templ.owner = shape->name;
            for (const GenericParam &generic : shape->generics) {
                templ.generics.push_back(generic.name);
            }
            for (const GenericParam &generic : method->generics) {
                templ.generics.push_back(generic.name);
            }
            templates_.push_back(std::move(templ));
            const std::string key =
                interner_.text(shape->name) + "::" + interner_.text(method->name);
            by_name_[key].push_back(static_cast<u32>(templates_.size() - 1));
        }
    }
}

// ---------------------------------------------------------------------------
// Pass 2: rough signatures
// ---------------------------------------------------------------------------

void Checker::resolve_signatures() {
    for (FunctionTemplate &templ : templates_) {
        Substitution abstract;
        for (Symbol generic : templ.generics) {
            abstract[generic.index] = types_.param(generic);
        }
        for (const Param &param : templ.decl->params) {
            const Type *type = resolve_type(param.type, abstract);
            templ.rough_params.push_back(type ? type : types_.error());
        }
        const Type *result = resolve_type(templ.decl->result, abstract);
        templ.rough_result = result ? result : types_.void_type();
        if (templ.decl->is_async) {
            templ.rough_result = types_.task(templ.rough_result);
        }
    }
}

// ---------------------------------------------------------------------------
// Specialization
// ---------------------------------------------------------------------------

u32 Checker::specialize(u32 templ_index, std::vector<const Type *> arguments, Span span) {
    const FunctionTemplate &templ = templates_[templ_index];

    std::string key = templ.owner.valid()
                          ? interner_.text(templ.owner) + "::" + interner_.text(templ.name)
                          : interner_.text(templ.name);
    for (const Type *argument : arguments) {
        key += "$";
        key += types_.mangle(argument);
    }

    auto cached = specialization_index_.find(key);
    if (cached != specialization_index_.end()) return cached->second;

    if (specialization_depth_ >= kMaxSpecializationDepth) {
        diagnostics_
            .error(Code::RecursiveSpecialization,
                   "generic specialization of '" + interner_.text(templ.name) + "' is too deep")
            .label(span)
            .note("the limit is " + std::to_string(kMaxSpecializationDepth) + " nested levels")
            .with_help("recursive generics must make structural progress toward a concrete type");
        return 0xFFFFFFFFu;
    }

    Specialization specialization;
    specialization.templ = templ_index;
    specialization.arguments = arguments;
    specialization.mangled = key;

    Substitution subst;
    for (std::size_t i = 0; i < templ.generics.size() && i < arguments.size(); ++i) {
        subst[templ.generics[i].index] = arguments[i];
    }
    for (const Type *rough : templ.rough_params) {
        specialization.params.push_back(substitute(rough, subst));
    }
    specialization.result = substitute(templ.rough_result, subst);

    const u32 index = static_cast<u32>(specializations_.size());
    specializations_.push_back(std::make_unique<Specialization>(std::move(specialization)));
    specialization_index_.emplace(key, index);
    worklist_.push_back(index);
    return index;
}

bool Checker::unify(const Type *pattern, const Type *actual, Substitution &subst) {
    if (!pattern || !actual) return true;
    if (pattern->kind == TypeKind::Param) {
        auto existing = subst.find(pattern->name.index);
        if (existing != subst.end()) {
            // Every occurrence of a parameter must resolve to the same type;
            // PunPun never silently widens to a common supertype.
            return existing->second == actual;
        }
        subst[pattern->name.index] = actual;
        return true;
    }
    if (pattern->kind != actual->kind) return false;
    if (pattern->element || actual->element) {
        return unify(pattern->element, actual->element, subst);
    }
    if (pattern->name != actual->name) return false;
    if (pattern->arguments.size() != actual->arguments.size()) return false;
    for (std::size_t i = 0; i < pattern->arguments.size(); ++i) {
        if (!unify(pattern->arguments[i], actual->arguments[i], subst)) return false;
    }
    return true;
}

bool Checker::infer_arguments(const FunctionTemplate &templ,
                              const std::vector<const Type *> &actual,
                              const std::vector<const Type *> &explicit_args, const Type *expected,
                              std::vector<const Type *> &out, Span span) {
    if (templ.generics.empty()) {
        out.clear();
        return true;
    }

    Substitution subst;
    // Explicit arguments bind leftmost-first and are never overridden.
    for (std::size_t i = 0; i < explicit_args.size() && i < templ.generics.size(); ++i) {
        subst[templ.generics[i].index] = explicit_args[i];
    }

    // Arguments first, per spec: inference reads call arguments, then falls back
    // to the expected result type.
    for (std::size_t i = 0; i < actual.size() && i < templ.rough_params.size(); ++i) {
        if (!actual[i] || actual[i]->is_error()) continue;
        unify(templ.rough_params[i], actual[i], subst);
    }
    if (expected && !expected->is_error()) {
        unify(templ.rough_result, expected, subst);
    }

    out.clear();
    for (Symbol generic : templ.generics) {
        auto it = subst.find(generic.index);
        if (it == subst.end()) {
            diagnostics_
                .error(Code::UnconstrainedParameter,
                       "cannot infer type parameter '" + interner_.text(generic) + "' of '" +
                           interner_.text(templ.name) + "'")
                .label(span)
                .with_help("give the type argument explicitly, as in `" +
                           interner_.text(templ.name) + "<int>(...)`");
            return false;
        }
        out.push_back(it->second);
    }
    return true;
}

bool Checker::type_meets_contract(const Type *type, Symbol contract) const {
    const std::string name = interner_.text(contract);
    // `Copy` is compiler-known rather than user-implementable.
    if (name == "Copy") return types_.is_copy(type);
    if (!type) return false;
    if (type->kind == TypeKind::Param) return true;  // checked at the call site instead
    if (type->is_aggregate() && type->kind != TypeKind::Enum) {
        if (type->decl >= types_.struct_count()) return false;
        const StructInfo &info = types_.struct_at(type->decl);
        return std::find(info.contracts.begin(), info.contracts.end(), contract) !=
               info.contracts.end();
    }
    return false;
}

bool Checker::satisfies_constraints(const FunctionTemplate &templ,
                                    const std::vector<const Type *> &arguments, Span span) {
    if (!templ.decl) return true;
    bool ok = true;

    // Constraints come from the function's own generics; for a method the
    // owner's parameters are prepended, so index from the end.
    const std::size_t own = templ.decl->generics.size();
    const std::size_t offset = templ.generics.size() >= own ? templ.generics.size() - own : 0;

    for (std::size_t i = 0; i < own; ++i) {
        const GenericParam &generic = templ.decl->generics[i];
        const std::size_t position = offset + i;
        if (position >= arguments.size()) break;
        const Type *argument = arguments[position];
        for (const TypeExpr *constraint : generic.constraints) {
            if (!constraint || constraint->kind != TypeExpr::Kind::Named) continue;
            if (type_meets_contract(argument, constraint->name)) continue;
            ok = false;
            const std::string contract = interner_.text(constraint->name);
            Diagnostic &diagnostic = diagnostics_.error(
                Code::ConstraintUnsatisfied,
                types_.describe(argument) + " does not satisfy the constraint '" + contract + "'");
            diagnostic.label(span, "required by '" + interner_.text(templ.name) + "'");
            if (contract == "Copy") {
                diagnostic.note(types_.describe(argument) +
                                " owns a resource, so it cannot be copied");
                diagnostic.with_help("pass it with `move(...)`, or borrow it with `&`");
            } else {
                diagnostic.with_help("declare the type with `meets " + contract + "`");
            }
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

HirProgram *Checker::check(const Program &program) {
    program_ = arena_.make<HirProgram>();
    for (const Module *module : program.modules) {
        for (const InjectionDecl &injection : module->injections) {
            program_->injections.push_back(injection);
        }
    }

    collect(program);
    if (diagnostics_.has_errors()) return program_;
    resolve_signatures();

    // Find the entry point and seed the worklist from it. Everything reachable
    // is specialized transitively; unreferenced generics are never instantiated.
    u32 entry_template = 0xFFFFFFFFu;
    for (u32 i = 0; i < templates_.size(); ++i) {
        if (!templates_[i].is_entry) continue;
        if (entry_template != 0xFFFFFFFFu) {
            diagnostics_
                .error(Code::MultipleEntryPoints, "the program has more than one entry point")
                .label(templates_[i].decl->span, "second entry point here")
                .secondary(templates_[entry_template].decl->span, "first one here")
                .with_help("a program has exactly one `launch` block or `fn main`");
            break;
        }
        entry_template = i;
    }

    if (entry_template == 0xFFFFFFFFu) {
        diagnostics_
            .error(Code::NoEntryPoint, "the program has no entry point")
            .with_help("add a `launch { ... }` block or a `fn main()`");
        return program_;
    }

    if (!templates_[entry_template].generics.empty()) {
        diagnostics_
            .error(Code::FeatureUnsupported, "the entry point cannot be generic")
            .label(templates_[entry_template].decl->span);
        return program_;
    }

    const u32 entry = specialize(entry_template, {}, templates_[entry_template].decl->span);

    // Also specialize every non-generic free function so a library-only build
    // still type-checks its whole surface.
    for (u32 i = 0; i < templates_.size(); ++i) {
        if (!templates_[i].generics.empty()) continue;
        if (templates_[i].owner.valid()) continue;
        if (templates_[i].decl->is_extern_native) continue;
        specialize(i, {}, templates_[i].decl->span);
    }

    // Drain the worklist. Checking a function can enqueue new specializations,
    // so the loop re-reads the size each iteration.
    while (!worklist_.empty()) {
        const u32 index = worklist_.back();
        worklist_.pop_back();
        if (index == 0xFFFFFFFFu) continue;
        if (spec_at(index).checked) continue;
        check_function(index);
        if (diagnostics_.limit_reached()) break;
    }

    for (std::unique_ptr<Specialization> &specialization : specializations_) {
        if (specialization->hir) program_->functions.push_back(specialization->hir);
    }
    if (entry != 0xFFFFFFFFu && spec_at(entry).hir) {
        for (u32 i = 0; i < program_->functions.size(); ++i) {
            if (program_->functions[i] == spec_at(entry).hir) {
                program_->entry = i;
                break;
            }
        }
    }
    return program_;
}

void Checker::check_function(u32 index) {
    Specialization &specialization = spec_at(index);
    specialization.checked = true;

    const FunctionTemplate &templ = templates_[specialization.templ];
    const FunctionDecl *decl = templ.decl;

    HirFunction *fn = arena_.make<HirFunction>();
    fn->name = specialization.mangled;
    fn->source_name = decl->name;
    fn->span = decl->span;
    fn->result = specialization.result;
    fn->is_entry = decl->is_entry;
    fn->is_async = decl->is_async;
    fn->is_extern_native = decl->is_extern_native;
    if (decl->is_extern_native) fn->native_symbol = interner_.text(decl->native_symbol);
    specialization.hir = fn;

    if (decl->is_extern_native) return;  // no body to check

    // Save and restore per-function state so a nested specialization triggered
    // mid-body does not clobber the outer function's context.
    HirFunction *saved_function = function_;
    const Specialization *saved_current = current_;
    Substitution saved_subst = subst_;
    auto saved_scopes = std::move(scopes_);
    const Type *saved_result = result_type_;
    auto saved_copy = copy_params_;
    const int saved_loop = loop_depth_;
    const bool saved_async = in_async_;

    function_ = fn;
    current_ = &specialization;
    scopes_.clear();
    loop_depth_ = 0;
    in_async_ = decl->is_async;
    ++specialization_depth_;

    subst_.clear();
    for (std::size_t i = 0; i < templ.generics.size() && i < specialization.arguments.size(); ++i) {
        subst_[templ.generics[i].index] = specialization.arguments[i];
    }

    // Record which parameters carry a `Copy` bound, so a generic body can copy
    // values of that parameter without the checker treating them as move-only.
    copy_params_.clear();
    for (const GenericParam &generic : decl->generics) {
        for (const TypeExpr *constraint : generic.constraints) {
            if (constraint && constraint->kind == TypeExpr::Kind::Named &&
                interner_.text(constraint->name) == "Copy") {
                copy_params_.push_back(generic.name);
            }
        }
    }

    // The result type is what `return` is checked against. For an async function
    // the declared type is the task payload, not the task itself.
    result_type_ = specialization.result;
    if (decl->is_async && result_type_ && result_type_->kind == TypeKind::Task) {
        result_type_ = result_type_->element;
    }
    // The lowered function *is* the task body, so it returns the payload. Only
    // a call site sees `task<T>`, and that comes from the specialization's own
    // result, which is left as-is.
    fn->result = result_type_;

    push_scope();

    // A method's receiver occupies slot 0. `self` is inserted implicitly when
    // the author did not write it.
    if (decl->is_method && templ.owner.valid()) {
        std::vector<const Type *> owner_arguments;
        const ShapeDecl *shape = shape_decls_.count(interner_.text(templ.owner))
                                     ? shape_decls_.at(interner_.text(templ.owner))
                                     : nullptr;
        if (shape) {
            for (std::size_t i = 0; i < shape->generics.size(); ++i) {
                owner_arguments.push_back(i < specialization.arguments.size()
                                              ? specialization.arguments[i]
                                              : types_.error());
            }
        }
        const Type *owner_type = instantiate_named(templ.owner, owner_arguments, decl->span);

        // An `object` is already a handle, so a method can mutate through it
        // directly. A value struct is copied into the receiver, so a method that
        // writes to `self` needs an explicit mutable borrow for the write to be
        // visible to the caller. Initializers always write, so they always
        // borrow.
        const Type *self_type = owner_type;
        if (owner_type && owner_type->kind == TypeKind::Struct &&
            (decl->is_initializer || decl->self_mutable)) {
            self_type = types_.reference(owner_type, true);
        }
        declare_local(interner_.intern("self"), self_type, decl->self_mutable, true, decl->span);
    }

    for (std::size_t i = 0; i < decl->params.size(); ++i) {
        const Type *type = i < specialization.params.size()
                               ? substitute(specialization.params[i], subst_)
                               : types_.error();
        declare_local(decl->params[i].name, type, decl->params[i].is_mutable, true,
                      decl->params[i].span);
    }
    fn->param_count = static_cast<u32>(fn->locals.size());

    check_block(decl->body, fn->body);

    // A non-void function must return on every path. The check is deliberately
    // conservative: it looks for a terminating statement rather than doing full
    // flow analysis, which matches the language reference.
    if (result_type_ && result_type_->kind != TypeKind::Void && !fn->body.empty()) {
        std::function<bool(const std::vector<HirStmt *> &)> always_returns =
            [&](const std::vector<HirStmt *> &body) -> bool {
            for (auto it = body.rbegin(); it != body.rend(); ++it) {
                const HirStmt *statement = *it;
                if (statement->kind == HirStmt::Kind::Return) return true;
                if (statement->kind == HirStmt::Kind::If && !statement->alternative.empty()) {
                    if (always_returns(statement->body) && always_returns(statement->alternative)) {
                        return true;
                    }
                }
                if (statement->kind == HirStmt::Kind::Block && always_returns(statement->body)) {
                    return true;
                }
            }
            return false;
        };
        if (!always_returns(fn->body)) {
            diagnostics_
                .error(Code::MissingReturn,
                       "function '" + interner_.text(decl->name) +
                           "' must return a value on every path")
                .label(decl->span, "declared to return " + types_.describe(result_type_))
                .with_help("add a `return` at the end, or handle the remaining branch");
        }
    } else if (result_type_ && result_type_->kind != TypeKind::Void && fn->body.empty()) {
        diagnostics_
            .error(Code::MissingReturn,
                   "function '" + interner_.text(decl->name) + "' has an empty body but returns " +
                       types_.describe(result_type_))
            .label(decl->span);
    }

    pop_scope();
    --specialization_depth_;

    function_ = saved_function;
    current_ = saved_current;
    subst_ = std::move(saved_subst);
    scopes_ = std::move(saved_scopes);
    result_type_ = saved_result;
    copy_params_ = std::move(saved_copy);
    loop_depth_ = saved_loop;
    in_async_ = saved_async;
}

}  // namespace ppc
