#include "ppc/sema/checker.hpp"

#include <algorithm>
#include <unordered_map>

namespace ppc {

// ---------------------------------------------------------------------------
// Expression dispatch
// ---------------------------------------------------------------------------

HirExpr *Checker::check_expr(const Expr *expr, const Type *expected) {
    if (!expr) return poison(Span{});

    switch (expr->kind) {
        case Expr::Kind::IntLiteral: {
            HirExpr *node = make_expr(HirExpr::Kind::ConstInt, expr->span, types_.int_type());
            node->int_value = expr->int_value;
            return node;
        }
        case Expr::Kind::FloatLiteral: {
            HirExpr *node = make_expr(HirExpr::Kind::ConstFloat, expr->span, types_.float_type());
            node->float_value = expr->float_value;
            return node;
        }
        case Expr::Kind::StringLiteral: {
            HirExpr *node = make_expr(HirExpr::Kind::ConstStr, expr->span, types_.str_type());
            node->string_value = expr->string_value;
            return node;
        }
        case Expr::Kind::BoolLiteral: {
            HirExpr *node = make_expr(HirExpr::Kind::ConstBool, expr->span, types_.bool_type());
            node->bool_value = expr->bool_value;
            return node;
        }
        case Expr::Kind::ListLiteral: {
            // A list literal builds a `nums` handle; every element must be int.
            HirExpr *node =
                make_expr(HirExpr::Kind::ListLiteral, expr->span, types_.nums_type());
            for (const Expr *element : expr->elements) {
                HirExpr *value = check_expr(element, types_.int_type());
                expect_type(value->type, types_.int_type(), element->span, "a list element");
                node->operands.push_back(value);
            }
            return node;
        }
        case Expr::Kind::SelfExpr: {
            LocalBinding *binding = lookup_local(interner_.intern("self"));
            if (!binding) {
                diagnostics_
                    .error(Code::UnknownName, "'self' is only available inside a method")
                    .label(expr->span);
                return poison(expr->span);
            }
            HirExpr *node = make_expr(HirExpr::Kind::Local, expr->span, binding->type);
            node->local = binding->slot;
            return node;
        }
        case Expr::Kind::Name: return check_name(expr, expected);
        case Expr::Kind::Path: return check_path(expr, expected);
        case Expr::Kind::Call: return check_call(expr, expected);
        case Expr::Kind::MethodCall: return check_method_call(expr, expected);
        case Expr::Kind::Field: return check_field(expr);
        case Expr::Kind::Index: return check_index(expr);
        case Expr::Kind::Unary: return check_unary(expr, expected);
        case Expr::Kind::Binary: return check_binary(expr);
        case Expr::Kind::Match: return check_match(expr, expected);
        case Expr::Kind::Propagate: return check_propagate(expr);
        case Expr::Kind::SizeOf:
        case Expr::Kind::AlignOf: {
            // Both fold to a constant during MIR construction; the type operand
            // only has to resolve.
            resolve_type(expr->type_operand, subst_);
            HirExpr *node = make_expr(HirExpr::Kind::ConstInt, expr->span, types_.int_type());
            node->int_value = 8;
            return node;
        }
        case Expr::Kind::Range:
            diagnostics_
                .error(Code::FeatureUnsupported, "a range is only valid as a loop iterable")
                .label(expr->span)
                .with_help("write `for i in start..end`");
            return poison(expr->span);
        case Expr::Kind::Assign:
            diagnostics_
                .error(Code::FeatureUnsupported, "assignment is a statement, not an expression")
                .label(expr->span);
            return poison(expr->span);
    }
    return poison(expr->span);
}

// ---------------------------------------------------------------------------
// Names and paths
// ---------------------------------------------------------------------------

HirExpr *Checker::check_name(const Expr *expr, const Type *expected) {
    // Locals shadow everything else.
    if (LocalBinding *binding = lookup_local(expr->name)) {
        reject_if_moved(expr->name, expr->span);
        HirExpr *node = make_expr(HirExpr::Kind::Local, expr->span, binding->type);
        node->local = binding->slot;
        return node;
    }

    const std::string name = interner_.text(expr->name);

    // A bare type name used as a value is a zero-argument constructor call.
    if (shape_decls_.count(name)) {
        const Type *type = instantiate_named(expr->name, {}, expr->span);
        if (type && !type->is_error()) {
            Expr synthetic = *expr;
            synthetic.arguments.clear();
            return check_constructor(&synthetic, type, type->decl);
        }
    }

    // A function named without parentheses is a function value.
    if (by_name_.count(name)) return check_function_value(expr, expected);
    if (find_builtin(name) != kNotBuiltin) {
        diagnostics_
            .error(Code::NotCallable, "'" + name + "' is a builtin and must be called")
            .label(expr->span)
            .with_help("write `" + name + "(...)`");
        return poison(expr->span);
    }

    (void)expected;
    if (in_lambda_) {
        // The name may well exist in the enclosing function. Saying "not found"
        // would send the reader hunting for a typo that is not there.
        diagnostics_
            .error(Code::UnknownName,
                   "a function literal cannot use '" + name + "' from around it")
            .label(expr->span)
            .note("function literals do not capture; they only see their own "
                  "parameters and module-level names")
            .with_help("pass '" + name + "' in as a parameter");
        return poison(expr->span);
    }
    diagnostics_.error(Code::UnknownName, "cannot find '" + name + "' in this scope")
        .label(expr->span)
        .with_help("check the spelling, or declare it with `let " + name + " = ...`");
    return poison(expr->span);
}

/// Call through a value of function type.
///
/// The callee's type carries the full signature, so arity and argument types
/// are checked here exactly as they are for a direct call; what is missing is
/// only the callee's identity, which is decided at run time.
HirExpr *Checker::check_indirect_call(const Expr *expr, HirExpr *callee) {
    const Type *type = callee->type;
    if (!type || type->is_error()) return poison(expr->span);

    if (expr->arguments.size() != type->arguments.size()) {
        diagnostics_
            .error(Code::ArityMismatch,
                   types_.describe(type) + " takes " +
                       std::to_string(type->arguments.size()) + " argument(s), found " +
                       std::to_string(expr->arguments.size()))
            .label(expr->span);
        return poison(expr->span);
    }

    HirExpr *node = make_expr(HirExpr::Kind::CallIndirect, expr->span, type->element);
    node->left = callee;
    for (std::size_t i = 0; i < expr->arguments.size(); ++i) {
        HirExpr *argument = check_expr(expr->arguments[i].value, type->arguments[i]);
        expect_type(argument->type, type->arguments[i], expr->arguments[i].span,
                    "this argument");
        node->operands.push_back(argument);
    }
    return node;
}

/// A function named without parentheses, used as a value.
///
/// The value is the callee's specialization index, so producing one means
/// selecting an overload and specializing it. Selection uses the expected type
/// when there is one, which is what lets two same-named functions be told apart
/// by the parameter they are being passed to.
HirExpr *Checker::check_function_value(const Expr *expr, const Type *expected) {
    const std::string name = interner_.text(expr->name);
    const auto candidates = by_name_.find(name);
    const std::vector<u32> &overloads = candidates->second;

    const Type *wanted =
        (expected && expected->kind == TypeKind::Function) ? expected : nullptr;

    std::vector<u32> viable;
    for (u32 index : overloads) {
        const FunctionTemplate &templ = templates_[index];
        // A generic function has no single address, so it cannot be a value
        // until it is applied to type arguments.
        if (!templ.generics.empty()) continue;
        if (wanted) {
            if (templ.rough_params.size() != wanted->arguments.size()) continue;
            bool matches = true;
            for (std::size_t i = 0; i < templ.rough_params.size(); ++i) {
                if (templ.rough_params[i] != wanted->arguments[i]) matches = false;
            }
            if (templ.rough_result != wanted->element) matches = false;
            if (!matches) continue;
        }
        viable.push_back(index);
    }

    if (viable.empty()) {
        auto builder =
            diagnostics_.error(Code::TypeMismatch,
                               "no version of '" + name + "' can be used as a value here")
                .label(expr->span);
        if (wanted) {
            builder.note("expected " + types_.describe(wanted));
        } else {
            builder.with_help("a generic function needs its type arguments before it "
                              "can be a value");
        }
        return poison(expr->span);
    }
    if (viable.size() > 1) {
        diagnostics_
            .error(Code::AmbiguousOverload, "'" + name + "' is overloaded, so it is ambiguous here")
            .label(expr->span)
            .with_help("annotate the binding with the function type you mean");
        return poison(expr->span);
    }

    const u32 specialization = specialize(viable.front(), {}, expr->span);
    const Specialization &info = spec_at(specialization);
    HirExpr *node = make_expr(HirExpr::Kind::FuncRef, expr->span,
                              types_.function(info.params, info.result));
    node->target = specialization;
    return node;
}

HirExpr *Checker::check_path(const Expr *expr, const Type *expected) {
    const std::string qualifier = interner_.text(expr->qualifier);
    const std::string member = interner_.text(expr->name);

    auto enum_it = enum_decls_.find(qualifier);
    if (enum_it == enum_decls_.end()) {
        diagnostics_.error(Code::UnknownType, "unknown enum '" + qualifier + "'")
            .label(expr->span)
            .with_help("variants are written `Enum::Variant`");
        return poison(expr->span);
    }

    // Resolve the enum's type arguments: explicit ones win, then the expected
    // type supplies them, and anything still unknown stays abstract so the
    // payload can pin it down at the call site.
    std::vector<const Type *> arguments;
    for (const TypeExpr *argument : expr->type_arguments) {
        const Type *resolved = resolve_type(argument, subst_);
        arguments.push_back(resolved ? resolved : types_.error());
    }
    if (arguments.empty() && expected && expected->kind == TypeKind::Enum &&
        interner_.text(expected->name) == qualifier) {
        arguments = expected->arguments;
    }
    if (arguments.empty() && !enum_it->second->generics.empty()) {
        // No context yet. `Option::None` with no expected type cannot pick T.
        diagnostics_
            .error(Code::CannotInfer,
                   "cannot infer the type arguments of '" + qualifier + "' here")
            .label(expr->span)
            .with_help("annotate the binding, as in `let x: " + qualifier + "<int> = ...`");
        return poison(expr->span);
    }

    const Type *enum_type = instantiate_named(expr->qualifier, arguments, expr->span);
    if (!enum_type || enum_type->is_error()) return poison(expr->span);

    const EnumInfo &info = types_.enum_at(enum_type->decl);
    for (u32 i = 0; i < info.variants.size(); ++i) {
        if (interner_.text(info.variants[i].name) != member) continue;
        if (!info.variants[i].payload.empty()) {
            diagnostics_
                .error(Code::PatternArity,
                       "variant '" + qualifier + "::" + member + "' carries " +
                           std::to_string(info.variants[i].payload.size()) + " value(s)")
                .label(expr->span)
                .with_help("construct it with `" + qualifier + "::" + member + "(...)`");
            return poison(expr->span);
        }
        return check_enum_construct(expr, enum_type, i, {});
    }

    diagnostics_
        .error(Code::UnknownVariant, "enum '" + qualifier + "' has no variant '" + member + "'")
        .label(expr->span);
    return poison(expr->span);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HirExpr *Checker::check_enum_construct(const Expr *expr, const Type *enum_type, u32 variant,
                                       const std::vector<const Expr *> &payload) {
    const EnumInfo &info = types_.enum_at(enum_type->decl);
    const VariantInfo &variant_info = info.variants[variant];

    HirExpr *node = make_expr(HirExpr::Kind::MakeEnum, expr->span, enum_type);
    node->target = enum_type->decl;
    node->field = variant;

    if (payload.size() != variant_info.payload.size()) {
        diagnostics_
            .error(Code::PatternArity,
                   "variant '" + interner_.text(info.name) + "::" +
                       interner_.text(variant_info.name) + "' takes " +
                       std::to_string(variant_info.payload.size()) + " value(s) but " +
                       std::to_string(payload.size()) + " were given")
            .label(expr->span);
        return node;
    }

    for (std::size_t i = 0; i < payload.size(); ++i) {
        HirExpr *value = check_expr(payload[i], variant_info.payload[i]);
        expect_type(value->type, variant_info.payload[i], payload[i]->span,
                    "an enum payload");
        note_move(payload[i], value);
        node->operands.push_back(value);
    }
    return node;
}

HirExpr *Checker::check_constructor(const Expr *expr, const Type *type, u32 struct_index) {
    const StructInfo &info = types_.struct_at(struct_index);

    // A type with an `init` allocates first and then runs the initializer for
    // its effect. That is three steps, so it lowers to a block: bind a
    // temporary, call `init` on it, then yield the temporary. Doing this here
    // means no later phase has to know that a constructor is not a plain call.
    if (info.decl && info.decl->initializer) {
        const std::string key =
            interner_.text(info.name) + "::" + interner_.text(info.decl->initializer->name);
        auto candidates = by_name_.find(key);
        if (candidates != by_name_.end() && !candidates->second.empty()) {
            const u32 templ_index = candidates->second.front();
            const u32 specialization = specialize(templ_index, type->arguments, expr->span);
            if (specialization == 0xFFFFFFFFu) return poison(expr->span);
            const Specialization &spec = spec_at(specialization);

            HirExpr *allocate = make_expr(HirExpr::Kind::MakeStruct, expr->span, type);
            allocate->target = struct_index;
            for (u32 i = 0; i < info.hidden_fields; ++i) {
                HirExpr *identity =
                    make_expr(HirExpr::Kind::ConstInt, expr->span, types_.int_type());
                identity->int_value = static_cast<i64>(struct_index);
                allocate->operands.push_back(identity);
            }

            const u32 slot =
                declare_local(interner_.intern("$new"), type, true, false, expr->span);

            HirStmt *bind = make_stmt(HirStmt::Kind::Let, expr->span);
            bind->local = slot;
            bind->value = allocate;

            HirExpr *receiver = make_expr(HirExpr::Kind::Local, expr->span, type);
            receiver->local = slot;

            // An object is a handle already; a value struct has to be borrowed
            // so the initializer's writes land on this temporary.
            HirExpr *self_argument = receiver;
            if (type->kind == TypeKind::Struct) {
                HirExpr *borrow = make_expr(HirExpr::Kind::Ref, expr->span,
                                            types_.reference(type, true));
                borrow->left = receiver;
                borrow->mutable_ref = true;
                self_argument = borrow;
            }

            HirExpr *call = make_expr(HirExpr::Kind::Call, expr->span, types_.void_type());
            call->target = specialization;
            call->operands.push_back(self_argument);

            std::vector<HirExpr *> bound;
            if (!bind_arguments(expr->arguments, info.decl->initializer->params, spec.params,
                                expr->span, bound, true)) {
                return poison(expr->span);
            }
            for (HirExpr *argument : bound) call->operands.push_back(argument);

            HirStmt *run = make_stmt(HirStmt::Kind::Expression, expr->span);
            run->value = call;

            HirExpr *result = make_expr(HirExpr::Kind::Local, expr->span, type);
            result->local = slot;

            HirExpr *block = make_expr(HirExpr::Kind::Block, expr->span, type);
            block->body.push_back(bind);
            block->body.push_back(run);
            block->left = result;
            return block;
        }
    }

    // Without an initializer the arguments bind directly to fields, either
    // positionally or by name.
    HirExpr *node = make_expr(HirExpr::Kind::MakeStruct, expr->span, type);
    node->target = struct_index;

    std::vector<Param> pseudo;
    std::vector<const Type *> field_types;
    for (const FieldInfo &field : info.fields) {
        if (field.is_hidden) continue;  // supplied by the compiler, not the author
        Param param;
        param.name = field.name;
        param.span = field.span;
        pseudo.push_back(param);
        field_types.push_back(field.type);
    }

    std::vector<HirExpr *> bound;
    if (!bind_arguments(expr->arguments, pseudo, field_types, expr->span, bound, true)) {
        return node;
    }
    // Hidden fields lead, so their values are prepended in declaration order.
    for (u32 i = 0; i < info.hidden_fields; ++i) {
        HirExpr *identity = make_expr(HirExpr::Kind::ConstInt, expr->span, types_.int_type());
        identity->int_value = static_cast<i64>(struct_index);
        node->operands.push_back(identity);
    }
    for (HirExpr *argument : bound) node->operands.push_back(argument);
    return node;
}

// ---------------------------------------------------------------------------
// Argument binding
// ---------------------------------------------------------------------------

std::vector<const Type *> Checker::probe_argument_types(
    const std::vector<Argument> &arguments) {
    SpeculativeScope guard(*this);
    std::vector<const Type *> result;
    result.reserve(arguments.size());
    for (const Argument &argument : arguments) {
        if (argument.name.valid()) {
            result.push_back(nullptr);
            continue;
        }
        HirExpr *probe = check_expr(argument.value, nullptr);
        result.push_back(probe->type);
    }
    return result;
}

bool Checker::bind_arguments(const std::vector<Argument> &arguments,
                             const std::vector<Param> &params,
                             const std::vector<const Type *> &param_types, Span span,
                             std::vector<HirExpr *> &out, bool report) {
    const std::size_t count = params.size();
    std::vector<const Expr *> slots(count, nullptr);
    std::vector<Span> spans(count);

    std::size_t positional = 0;
    for (const Argument &argument : arguments) {
        if (!argument.name.valid()) {
            if (positional >= count) {
                if (report) {
                    diagnostics_
                        .error(Code::ArityMismatch,
                               "too many arguments: this takes " + std::to_string(count))
                        .label(argument.span);
                }
                return false;
            }
            slots[positional] = argument.value;
            spans[positional] = argument.span;
            ++positional;
            continue;
        }

        // Named arguments identify their parameter before any inference runs.
        bool found = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (params[i].name != argument.name) continue;
            if (slots[i]) {
                if (report) {
                    diagnostics_
                        .error(Code::DuplicateArgument,
                               "argument '" + interner_.text(argument.name) + "' is given twice")
                        .label(argument.span);
                }
                return false;
            }
            slots[i] = argument.value;
            spans[i] = argument.span;
            found = true;
            break;
        }
        if (!found) {
            if (report) {
                diagnostics_
                    .error(Code::UnknownArgument,
                           "no parameter named '" + interner_.text(argument.name) + "'")
                    .label(argument.span);
            }
            return false;
        }
    }

    out.clear();
    for (std::size_t i = 0; i < count; ++i) {
        const Type *wanted = i < param_types.size() ? param_types[i] : types_.error();
        wanted = substitute(wanted, subst_);

        if (slots[i]) {
            HirExpr *value = check_expr(slots[i], wanted);
            if (report) {
                const std::string context =
                    "argument '" + interner_.text(params[i].name) + "'";
                expect_type(value->type, wanted, slots[i]->span, context.c_str());
            }
            note_move(slots[i], value);
            out.push_back(value);
            continue;
        }

        // Defaults are substituted only after the parameter went unmatched.
        if (params[i].default_value) {
            HirExpr *value = check_expr(params[i].default_value, wanted);
            out.push_back(value);
            continue;
        }

        if (report) {
            diagnostics_
                .error(Code::ArityMismatch,
                       "missing argument '" + interner_.text(params[i].name) + "'")
                .label(span, "this call needs " + std::to_string(count) + " argument(s)")
                .note("parameter '" + interner_.text(params[i].name) + "' has no default");
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

HirExpr *Checker::check_builtin_call(const Expr *expr, BuiltinId builtin,
                                    const Type *expected) {
    const BuiltinSpec &spec = builtin_table()[builtin];
    const std::string name = spec.name;

    if (expr->arguments.size() != spec.params.size()) {
        diagnostics_
            .error(Code::ArityMismatch,
                   "'" + name + "' takes " + std::to_string(spec.params.size()) +
                       " argument(s) but " + std::to_string(expr->arguments.size()) +
                       " were given")
            .label(expr->span)
            .note("signature: " + builtin_signature(spec));
        return poison(expr->span);
    }

    HirExpr *node = make_expr(HirExpr::Kind::CallBuiltin, expr->span, types_.void_type());
    node->builtin = builtin;

    // The list builtins are polymorphic in their element type, and that type is
    // read from the list argument rather than spelled at the call site. Probe
    // argument 0 first so `list_push(items, x)` can check `x` against whatever
    // `items` actually holds.
    const Type *list_element = nullptr;
    const Type *map_value = nullptr;
    for (std::size_t i = 0; i < spec.params.size(); ++i) {
        if (spec.params[i] != BuiltinType::AnyMap) continue;
        if (i >= expr->arguments.size()) break;
        const std::vector<const Type *> probed = probe_argument_types({expr->arguments[i]});
        if (probed.empty() || !probed[0]) break;
        if (probed[0]->kind == TypeKind::Map) {
            map_value = probed[0]->element;
        } else if (!probed[0]->is_error()) {
            diagnostics_
                .error(Code::TypeMismatch,
                       "'" + name + "' expects a Map, found " + types_.describe(probed[0]))
                .label(expr->arguments[i].span)
                .with_help("create one with `map<V>()`");
        }
        break;
    }
    for (std::size_t i = 0; i < spec.params.size(); ++i) {
        if (spec.params[i] != BuiltinType::AnyList) continue;
        if (i >= expr->arguments.size()) break;
        const std::vector<const Type *> probed =
            probe_argument_types({expr->arguments[i]});
        if (probed.empty() || !probed[0]) break;
        if (probed[0]->kind == TypeKind::List) {
            list_element = probed[0]->element;
        } else if (!probed[0]->is_error()) {
            diagnostics_
                .error(Code::TypeMismatch,
                       "'" + name + "' expects a List, found " + types_.describe(probed[0]))
                .label(expr->arguments[i].span)
                .with_help("create one with `list<T>()`");
        }
        break;
    }

    for (std::size_t i = 0; i < expr->arguments.size(); ++i) {
        const BuiltinType wanted = spec.params[i];
        const Type *expected = builtin_type_to_type(wanted, types_);
        if (wanted == BuiltinType::ListElement) expected = list_element;
        if (wanted == BuiltinType::MapValue) expected = map_value;
        HirExpr *value = check_expr(expr->arguments[i].value, expected);

        if (expected) {
            const std::string context = "an argument to '" + name + "'";
            expect_type(value->type, expected, expr->arguments[i].span, context.c_str());
        } else if (wanted == BuiltinType::AnyScalar) {
            // print/println/say accept any single-word value and pick a runtime
            // formatter from the static type.
            if (value->type && !value->type->is_scalar() && !value->type->is_error()) {
                diagnostics_
                    .error(Code::InvalidOperand,
                           "'" + name + "' cannot print " + types_.describe(value->type))
                    .label(expr->arguments[i].span)
                    .with_help("convert it first, for example with `text(...)`");
            }
        } else if (wanted == BuiltinType::AnyTask) {
            if (value->type && value->type->kind != TypeKind::Task && !value->type->is_error()) {
                diagnostics_
                    .error(Code::NotAwaitable,
                           "'" + name + "' expects a task, found " + types_.describe(value->type))
                    .label(expr->arguments[i].span);
            }
        }
        node->operands.push_back(value);
    }

    // Result type. Most builtins are monomorphic; the handful that are not read
    // their result from the first argument.
    if (const Type *result = builtin_type_to_type(spec.result, types_)) {
        node->type = result;
    } else if (spec.result == BuiltinType::SameAsArgument) {
        node->type = node->operands.empty() ? types_.void_type() : node->operands[0]->type;
    } else if (spec.result == BuiltinType::ListElement) {
        node->type = list_element ? list_element : types_.error();
    } else if (spec.result == BuiltinType::MapValue) {
        node->type = map_value ? map_value : types_.error();
    } else if (spec.result == BuiltinType::NewMap) {
        const Type *value = nullptr;
        if (expr->left && !expr->left->type_arguments.empty()) {
            value = resolve_type(expr->left->type_arguments[0], subst_);
        } else if (expected && expected->kind == TypeKind::Map) {
            value = expected->element;
        }
        if (!value || value->is_error()) {
            diagnostics_
                .error(Code::CannotInfer, "cannot tell what this map will hold")
                .label(expr->span)
                .with_help("write `map<int>()`, or annotate the binding as "
                           "`let m: Map<int> = map()`");
            node->type = types_.error();
        } else {
            node->type = types_.map(value);
        }
    } else if (spec.result == BuiltinType::NewList) {
        // `list<str>()` names the element type; `let x: List<str> = list()`
        // supplies it from context. One of the two must be present, because a
        // list with no element type has no meaning.
        const Type *element = nullptr;
        if (expr->left && !expr->left->type_arguments.empty()) {
            element = resolve_type(expr->left->type_arguments[0], subst_);
        } else if (expected && expected->kind == TypeKind::List) {
            element = expected->element;
        }
        if (!element || element->is_error()) {
            diagnostics_
                .error(Code::CannotInfer, "cannot tell what this list will hold")
                .label(expr->span)
                .with_help("write `list<int>()`, or annotate the binding as "
                           "`let items: List<int> = list()`");
            node->type = types_.error();
        } else {
            node->type = types_.list(element);
        }
    } else if (spec.result == BuiltinType::AnyTask) {
        node->type = types_.void_type();
    } else {
        node->type = types_.void_type();
    }

    // `move` and `drop` end the source binding's lifetime.
    if (name == "move" || name == "drop") {
        if (!expr->arguments.empty() &&
            expr->arguments[0].value->kind == Expr::Kind::Name) {
            const Symbol source = expr->arguments[0].value->name;
            if (LocalBinding *binding = lookup_local(source)) {
                binding->moved = true;
                binding->move_span = expr->span;
                if (!node->operands.empty()) node->operands[0]->consumes = true;
            }
        } else if (!expr->arguments.empty()) {
            diagnostics_
                .error(Code::PartialMove, "'" + name + "' requires a named binding")
                .label(expr->arguments[0].span)
                .with_help("partial moves out of fields or elements are not allowed in 0.6");
        }
        if (name == "drop") node->type = types_.void_type();
    }

    // A borrow of `nums` must outlive nothing that mutates the source, so record
    // the slice's provenance for the escape check.
    if (name == "view" && !node->operands.empty() &&
        node->operands[0]->kind == HirExpr::Kind::Local) {
        node->slot = node->operands[0]->local + 1;  // biased so 0 means "none"
    }

    // Mutating a list invalidates any view into it: the view holds a start
    // offset and length that a resize can put out of date, and `sort` can move
    // the elements it points at. Both are rejected while a borrow is live.
    static const std::unordered_map<std::string, const char *> kMutators = {
        {"push", "appending to"}, {"put", "assigning into"},
        {"pop", "removing from"}, {"sort", "sorting"}};
    auto mutator = kMutators.find(name);
    if (mutator != kMutators.end() && !expr->arguments.empty() &&
        expr->arguments[0].value->kind == Expr::Kind::Name) {
        const Symbol source = expr->arguments[0].value->name;
        if (LocalBinding *binding = lookup_local(source); binding && binding->shared_borrows > 0) {
            diagnostics_
                .error(Code::BorrowConflict,
                       std::string(mutator->second) + " '" + interner_.text(source) +
                           "' is not allowed while it is borrowed")
                .label(expr->span, "this mutates the list")
                .secondary(binding->borrow_span, "a view of it is still live here")
                .note("a view holds an offset and length into the list, which this could "
                      "invalidate")
                .with_help("finish using the view first, or take it again afterwards");
        }
    }
    return node;
}

HirExpr *Checker::check_call(const Expr *expr, const Type *expected) {
    const Expr *callee = expr->left;
    if (!callee) return poison(expr->span);

    // A local holding a function value shadows the function table, so calling
    // it goes through the value rather than resolving a name.
    if (callee->kind == Expr::Kind::Name) {
        if (LocalBinding *binding = lookup_local(callee->name);
            binding && binding->type && binding->type->kind == TypeKind::Function) {
            return check_indirect_call(expr, check_expr(callee, nullptr));
        }
    }

    // `Enum::Variant(payload)`
    if (callee->kind == Expr::Kind::Path) {
        const std::string qualifier = interner_.text(callee->qualifier);
        auto enum_it = enum_decls_.find(qualifier);
        if (enum_it != enum_decls_.end()) {
            std::vector<const Type *> arguments;
            for (const TypeExpr *argument : callee->type_arguments) {
                const Type *resolved = resolve_type(argument, subst_);
                arguments.push_back(resolved ? resolved : types_.error());
            }
            if (arguments.empty() && expected && expected->kind == TypeKind::Enum &&
                interner_.text(expected->name) == qualifier) {
                arguments = expected->arguments;
            }

            // With no annotation, infer the enum's parameters from the payload.
            if (arguments.empty() && !enum_it->second->generics.empty()) {
                const EnumDecl *decl = enum_it->second;
                Substitution inferred;
                SpeculativeScope guard(*this);
                for (const EnumVariantDecl &variant : decl->variants) {
                    if (variant.name != callee->name) continue;
                    for (std::size_t i = 0;
                         i < variant.payload.size() && i < expr->arguments.size(); ++i) {
                        HirExpr *probe = check_expr(expr->arguments[i].value, nullptr);
                        if (!probe->type || probe->type->is_error()) continue;
                        Substitution local;
                        for (const GenericParam &generic : decl->generics) {
                            local[generic.name.index] = types_.param(generic.name);
                        }
                        const Type *pattern = resolve_type(variant.payload[i], local);
                        unify(pattern, probe->type, inferred);
                    }
                }
                for (const GenericParam &generic : decl->generics) {
                    auto found = inferred.find(generic.name.index);
                    // A parameter the payload does not mention (Option::None,
                    // Result::Error's T) cannot be inferred here.
                    arguments.push_back(found == inferred.end() ? types_.error()
                                                                : found->second);
                }
                for (const Type *argument : arguments) {
                    if (!argument->is_error()) continue;
                    diagnostics_
                        .error(Code::CannotInfer,
                               "cannot infer the type arguments of '" + qualifier + "' here")
                        .label(expr->span)
                        .with_help("annotate the binding or the function's return type");
                    return poison(expr->span);
                }
            }

            const Type *enum_type = instantiate_named(callee->qualifier, arguments, expr->span);
            if (!enum_type || enum_type->is_error()) return poison(expr->span);

            const EnumInfo &info = types_.enum_at(enum_type->decl);
            for (u32 i = 0; i < info.variants.size(); ++i) {
                if (info.variants[i].name != callee->name) continue;
                std::vector<const Expr *> payload;
                for (const Argument &argument : expr->arguments) payload.push_back(argument.value);
                return check_enum_construct(expr, enum_type, i, payload);
            }
            diagnostics_
                .error(Code::UnknownVariant,
                       "enum '" + qualifier + "' has no variant '" +
                           interner_.text(callee->name) + "'")
                .label(callee->span);
            return poison(expr->span);
        }
    }

    if (callee->kind != Expr::Kind::Name) {
        diagnostics_.error(Code::NotCallable, "this expression is not callable")
            .label(callee->span);
        return poison(expr->span);
    }

    const std::string name = interner_.text(callee->name);

    // A local shadowing a function name is not callable; say that clearly.
    if (lookup_local(callee->name)) {
        diagnostics_
            .error(Code::NotCallable, "'" + name + "' is a variable, not a function")
            .label(callee->span);
        return poison(expr->span);
    }

    // Type constructors.
    if (shape_decls_.count(name)) {
        std::vector<const Type *> arguments;
        for (const TypeExpr *argument : callee->type_arguments) {
            const Type *resolved = resolve_type(argument, subst_);
            arguments.push_back(resolved ? resolved : types_.error());
        }
        const ShapeDecl *decl = shape_decls_.at(name);
        // Infer the type arguments from the field initializers when the call
        // site did not spell them out.
        if (arguments.empty() && !decl->generics.empty()) {
            if (expected && expected->is_aggregate() &&
                interner_.text(expected->name) == name) {
                arguments = expected->arguments;
            } else {
                Substitution inferred;
                Substitution abstract;
                SpeculativeScope guard(*this);
                for (const GenericParam &generic : decl->generics) {
                    abstract[generic.name.index] = types_.param(generic.name);
                }
                for (std::size_t i = 0; i < decl->fields.size() && i < expr->arguments.size();
                     ++i) {
                    HirExpr *probe = check_expr(expr->arguments[i].value, nullptr);
                    if (!probe->type || probe->type->is_error()) continue;
                    unify(resolve_type(decl->fields[i].type, abstract), probe->type, inferred);
                }
                for (const GenericParam &generic : decl->generics) {
                    auto found = inferred.find(generic.name.index);
                    if (found == inferred.end()) {
                        diagnostics_
                            .error(Code::CannotInfer,
                                   "cannot infer type parameter '" +
                                       interner_.text(generic.name) + "' of '" + name + "'")
                            .label(expr->span)
                            .with_help("write the type arguments, as in `" + name + "<int>(...)`");
                        return poison(expr->span);
                    }
                    arguments.push_back(found->second);
                }
            }
        }
        const Type *type = instantiate_named(callee->name, arguments, expr->span);
        if (!type || type->is_error()) return poison(expr->span);
        return check_constructor(expr, type, type->decl);
    }

    // Builtins come before user functions so a program cannot accidentally
    // shadow the runtime surface.
    if (const BuiltinId builtin = find_builtin(name); builtin != kNotBuiltin) {
        return check_builtin_call(expr, builtin, expected);
    }

    auto candidates = by_name_.find(name);
    if (candidates == by_name_.end() || candidates->second.empty()) {
        diagnostics_.error(Code::UnknownName, "cannot find function '" + name + "'")
            .label(callee->span)
            .with_help("check the spelling, or import the module that defines it");
        return poison(expr->span);
    }

    std::vector<const Type *> explicit_args;
    for (const TypeExpr *argument : callee->type_arguments) {
        const Type *resolved = resolve_type(argument, subst_);
        explicit_args.push_back(resolved ? resolved : types_.error());
    }

    // Probe the argument types for overload selection and inference. The real
    // check happens later in bind_arguments, with the parameter type as
    // context, so this pass must leave no trace.
    const std::vector<const Type *> actual = probe_argument_types(expr->arguments);

    // Select an overload: arity first, then exact matches, then viable generics.
    std::vector<u32> viable;
    for (u32 index : candidates->second) {
        const FunctionTemplate &templ = templates_[index];
        const std::size_t required = std::count_if(
            templ.decl->params.begin(), templ.decl->params.end(),
            [](const Param &param) { return param.default_value == nullptr; });
        if (expr->arguments.size() < required ||
            expr->arguments.size() > templ.decl->params.size()) {
            continue;
        }
        viable.push_back(index);
    }

    if (viable.empty()) {
        const FunctionTemplate &first = templates_[candidates->second.front()];
        diagnostics_
            .error(Code::ArityMismatch,
                   "'" + name + "' cannot be called with " +
                       std::to_string(expr->arguments.size()) + " argument(s)")
            .label(expr->span)
            .secondary(first.decl->span,
                       "declared with " + std::to_string(first.decl->params.size()) +
                           " parameter(s)");
        return poison(expr->span);
    }

    u32 chosen = viable.front();
    if (viable.size() > 1) {
        // Prefer a candidate whose non-generic parameters match exactly.
        int best_score = -1;
        std::vector<u32> best;
        for (u32 index : viable) {
            const FunctionTemplate &templ = templates_[index];
            int score = 0;
            for (std::size_t i = 0; i < actual.size() && i < templ.rough_params.size(); ++i) {
                if (!actual[i]) continue;
                if (templ.rough_params[i] == actual[i]) score += 2;
                else if (templ.rough_params[i] &&
                         templ.rough_params[i]->kind == TypeKind::Param)
                    score += 1;
            }
            if (score > best_score) {
                best_score = score;
                best.assign(1, index);
            } else if (score == best_score) {
                best.push_back(index);
            }
        }
        if (best.size() > 1) {
            Diagnostic &diagnostic = diagnostics_.error(
                Code::AmbiguousOverload, "ambiguous call to '" + name + "'");
            diagnostic.label(expr->span);
            for (u32 index : best) {
                diagnostic.secondary(templates_[index].decl->span, "candidate here");
            }
            diagnostic.with_help("give the type arguments explicitly to disambiguate");
            return poison(expr->span);
        }
        chosen = best.front();
    }

    const FunctionTemplate &templ = templates_[chosen];
    std::vector<const Type *> type_arguments;
    if (!infer_arguments(templ, actual, explicit_args, expected, type_arguments, expr->span)) {
        return poison(expr->span);
    }
    if (!satisfies_constraints(templ, type_arguments, expr->span)) {
        return poison(expr->span);
    }

    const u32 specialization = specialize(chosen, type_arguments, expr->span);
    if (specialization == 0xFFFFFFFFu) return poison(expr->span);
    const Specialization &spec = spec_at(specialization);

    HirExpr *node = make_expr(HirExpr::Kind::Call, expr->span, spec.result);
    node->target = specialization;

    std::vector<HirExpr *> bound;
    if (!bind_arguments(expr->arguments, templ.decl->params, spec.params, expr->span, bound,
                        true)) {
        return poison(expr->span);
    }
    node->operands = std::move(bound);

    if (templ.decl->is_extern_native) {
        // A native call cannot be reasoned about, so it is only allowed where
        // the author has already accepted that risk.
        if (unsafe_depth_ == 0) {
            diagnostics_
                .error(Code::UnsafeRequired,
                       "calling native function '" + name + "' requires an unsafe block")
                .label(expr->span)
                .with_help("wrap the call in `unsafe { ... }`");
        }
    }
    return node;
}

/// Call a contract method on a value whose concrete type is not known here.
///
/// The receiver is an object handle and carries its own identity, so dispatch
/// reads that identity and selects among the types that declare they meet the
/// contract. It is expanded inline as a chain of comparisons rather than a
/// jump table: a table wants a new runtime structure and a fourth thing for
/// every backend to agree on, and the chain reuses HIR that already exists.
/// With many implementors that is linear per call, which is the price of this
/// first implementation and the reason to revisit it before the count grows.
HirExpr *Checker::check_contract_call(const Expr *expr, HirExpr *receiver,
                                      const Type *contract_type, const std::string &method) {
    const ContractInfo &contract = types_.contract_at(contract_type->decl);

    bool declares_method = false;
    for (Symbol name : contract.methods) {
        if (interner_.text(name) == method) declares_method = true;
    }
    if (!declares_method) {
        diagnostics_
            .error(Code::UnknownMethod,
                   "contract '" + interner_.text(contract.name) + "' has no method '" +
                       method + "'")
            .label(expr->span)
            .with_help("add it to the contract, or call through the concrete type");
        return poison(expr->span);
    }

    // Every type that declares it meets this contract is a candidate. The set is
    // whatever has been instantiated by now, which is why a program has to name
    // a type somewhere for it to participate.
    std::vector<u32> implementors;
    for (std::size_t index = 0; index < types_.struct_count(); ++index) {
        const StructInfo &info = types_.struct_at(static_cast<u32>(index));
        if (!info.is_reference) continue;
        for (Symbol declared : info.contracts) {
            if (declared == contract_type->name) implementors.push_back(static_cast<u32>(index));
        }
    }
    if (implementors.empty()) {
        diagnostics_
            .error(Code::UnknownMethod,
                   "no type meets contract '" + interner_.text(contract.name) + "'")
            .label(expr->span);
        return poison(expr->span);
    }

    // Arguments are checked once, against the first implementor's signature;
    // every implementor must agree on it because the contract fixes it.
    std::vector<HirExpr *> arguments;
    for (const Argument &argument : expr->arguments) {
        arguments.push_back(check_expr(argument.value, nullptr));
    }

    HirExpr *block = make_expr(HirExpr::Kind::Block, expr->span, types_.error());
    push_scope();

    // The receiver is evaluated once and reused by every branch.
    const u32 receiver_slot =
        declare_local(interner_.intern("__pp_self"), contract_type, false, false, expr->span);
    HirStmt *bind_receiver = make_stmt(HirStmt::Kind::Let, expr->span);
    bind_receiver->local = receiver_slot;
    bind_receiver->value = receiver;
    block->body.push_back(bind_receiver);

    auto read_receiver = [&](const Type *as) {
        // Widening and narrowing between a contract and an object are both the
        // identity on the representation, so this only restates the type.
        HirExpr *read = make_expr(HirExpr::Kind::Local, expr->span, as);
        read->local = receiver_slot;
        return read;
    };

    // The identity field is the object's first slot.
    HirExpr *identity = make_expr(HirExpr::Kind::Field, expr->span, types_.int_type());
    identity->left = read_receiver(contract_type);
    identity->field = 0;
    const u32 identity_slot =
        declare_local(interner_.intern("__pp_id"), types_.int_type(), false, false, expr->span);
    HirStmt *bind_identity = make_stmt(HirStmt::Kind::Let, expr->span);
    bind_identity->local = identity_slot;
    bind_identity->value = identity;
    block->body.push_back(bind_identity);

    const Type *result_type = nullptr;
    u32 result_slot = 0;
    HirStmt *chain_tail = nullptr;

    for (u32 implementor : implementors) {
        const StructInfo &info = types_.struct_at(implementor);
        const std::string key = interner_.text(info.name) + "::" + method;
        auto candidates = by_name_.find(key);
        if (candidates == by_name_.end() || candidates->second.empty()) {
            diagnostics_
                .error(Code::UnknownMethod,
                       "'" + interner_.text(info.name) + "' says it meets '" +
                           interner_.text(contract.name) + "' but does not define '" +
                           method + "'")
                .label(info.span);
            pop_scope();
            return poison(expr->span);
        }
        const u32 specialization = specialize(candidates->second.front(), {}, expr->span);
        if (specialization == 0xFFFFFFFFu) continue;
        const Specialization &spec = spec_at(specialization);

        const Type *object_type =
            types_.named(TypeKind::Object, info.name, info.arguments, implementor);

        HirExpr *call = make_expr(HirExpr::Kind::Call, expr->span, spec.result);
        call->target = specialization;
        call->operands.push_back(read_receiver(object_type));
        for (HirExpr *argument : arguments) call->operands.push_back(argument);

        if (!result_type) {
            result_type = spec.result;
            block->type = result_type;
            if (result_type->kind != TypeKind::Void) {
                result_slot = declare_local(interner_.intern("__pp_result"), result_type, true,
                                            false, expr->span);
                HirStmt *declare = make_stmt(HirStmt::Kind::Let, expr->span);
                declare->local = result_slot;
                block->body.push_back(declare);
            }
        }

        // `if identity == <this type> { result = call(...) } else <next>`
        HirExpr *tag = make_expr(HirExpr::Kind::ConstInt, expr->span, types_.int_type());
        tag->int_value = static_cast<i64>(implementor);
        HirExpr *test = make_expr(HirExpr::Kind::Binary, expr->span, types_.bool_type());
        test->binary_op = BinaryOp::Equal;
        HirExpr *left = make_expr(HirExpr::Kind::Local, expr->span, types_.int_type());
        left->local = identity_slot;
        test->left = left;
        test->right = tag;

        HirStmt *branch = make_stmt(HirStmt::Kind::If, expr->span);
        branch->value = test;
        if (result_type->kind == TypeKind::Void) {
            HirStmt *run = make_stmt(HirStmt::Kind::Expression, expr->span);
            run->value = call;
            branch->body.push_back(run);
        } else {
            HirStmt *assign = make_stmt(HirStmt::Kind::Assign, expr->span);
            HirExpr *place = make_expr(HirExpr::Kind::Local, expr->span, result_type);
            place->local = result_slot;
            assign->place = place;
            assign->value = call;
            branch->body.push_back(assign);
        }

        if (chain_tail) {
            chain_tail->alternative.push_back(branch);
        } else {
            block->body.push_back(branch);
        }
        chain_tail = branch;
    }

    if (!result_type) {
        pop_scope();
        return poison(expr->span);
    }

    if (result_type->kind == TypeKind::Void) {
        block->left = make_expr(HirExpr::Kind::ConstInt, expr->span, types_.void_type());
    } else {
        HirExpr *value = make_expr(HirExpr::Kind::Local, expr->span, result_type);
        value->local = result_slot;
        block->left = value;
    }
    pop_scope();
    return block;
}

HirExpr *Checker::check_method_call(const Expr *expr, const Type *expected) {
    HirExpr *receiver = check_expr(expr->left, nullptr);
    const Type *receiver_type = receiver->type;
    if (!receiver_type || receiver_type->is_error()) return poison(expr->span);

    // Method lookup sees through a borrow, so `reference.method()` works.
    const Type *owner_type = receiver_type;
    while (owner_type->is_pointer_like() && owner_type->element) owner_type = owner_type->element;

    const std::string method = interner_.text(expr->name);

    if (owner_type->kind == TypeKind::Contract) {
        return check_contract_call(expr, receiver, owner_type, method);
    }

    if (!owner_type->is_aggregate() || owner_type->kind == TypeKind::Enum) {
        diagnostics_
            .error(Code::UnknownMethod,
                   types_.describe(owner_type) + " has no method '" + method + "'")
            .label(expr->span);
        return poison(expr->span);
    }

    const StructInfo &info = types_.struct_at(owner_type->decl);
    const std::string key = interner_.text(info.name) + "::" + method;
    auto candidates = by_name_.find(key);
    if (candidates == by_name_.end() || candidates->second.empty()) {
        diagnostics_
            .error(Code::UnknownMethod,
                   types_.describe(owner_type) + " has no method '" + method + "'")
            .label(expr->span)
            .with_help("check the spelling, or add `fn " + method + "()` to the type");
        return poison(expr->span);
    }

    const u32 templ_index = candidates->second.front();
    const FunctionTemplate &templ = templates_[templ_index];

    if (templ.decl->visibility == Visibility::Private && current_ &&
        templates_[current_->templ].owner != info.name) {
        diagnostics_
            .error(Code::PrivateAccess, "method '" + method + "' is private")
            .label(expr->span)
            .secondary(templ.decl->span, "declared private here");
    }

    // The owner's type arguments come first, then the method's own.
    std::vector<const Type *> type_arguments = owner_type->arguments;
    const std::vector<const Type *> actual = probe_argument_types(expr->arguments);

    if (!templ.decl->generics.empty()) {
        Substitution subst;
        for (std::size_t i = 0; i < type_arguments.size() && i < templ.generics.size(); ++i) {
            subst[templ.generics[i].index] = type_arguments[i];
        }
        for (std::size_t i = 0; i < actual.size() && i < templ.rough_params.size(); ++i) {
            if (!actual[i]) continue;
            unify(templ.rough_params[i], actual[i], subst);
        }
        if (expected) unify(templ.rough_result, expected, subst);
        for (std::size_t i = type_arguments.size(); i < templ.generics.size(); ++i) {
            auto found = subst.find(templ.generics[i].index);
            if (found == subst.end()) {
                diagnostics_
                    .error(Code::UnconstrainedParameter,
                           "cannot infer type parameter '" +
                               interner_.text(templ.generics[i]) + "' of '" + method + "'")
                    .label(expr->span);
                return poison(expr->span);
            }
            type_arguments.push_back(found->second);
        }
    }

    if (!satisfies_constraints(templ, type_arguments, expr->span)) return poison(expr->span);

    const u32 specialization = specialize(templ_index, type_arguments, expr->span);
    if (specialization == 0xFFFFFFFFu) return poison(expr->span);
    const Specialization &spec = spec_at(specialization);

    HirExpr *node = make_expr(HirExpr::Kind::Call, expr->span, spec.result);
    node->target = specialization;
    node->operands.push_back(receiver);

    std::vector<HirExpr *> bound;
    if (!bind_arguments(expr->arguments, templ.decl->params, spec.params, expr->span, bound,
                        true)) {
        return poison(expr->span);
    }
    for (HirExpr *argument : bound) node->operands.push_back(argument);
    return node;
}

// ---------------------------------------------------------------------------
// Field and index access
// ---------------------------------------------------------------------------

HirExpr *Checker::check_field(const Expr *expr) {
    HirExpr *base = check_expr(expr->left, nullptr);
    const Type *type = base->type;
    if (!type || type->is_error()) return poison(expr->span);

    while (type->is_pointer_like() && type->element) type = type->element;

    if (!type->is_aggregate() || type->kind == TypeKind::Enum) {
        diagnostics_
            .error(Code::UnknownField,
                   types_.describe(type) + " has no fields")
            .label(expr->span);
        return poison(expr->span);
    }

    const StructInfo &info = types_.struct_at(type->decl);
    for (u32 i = 0; i < info.fields.size(); ++i) {
        if (info.fields[i].name != expr->name) continue;
        if (info.fields[i].visibility == Visibility::Private && current_ &&
            templates_[current_->templ].owner != info.name) {
            diagnostics_
                .error(Code::PrivateAccess,
                       "field '" + interner_.text(expr->name) + "' is private")
                .label(expr->span)
                .secondary(info.fields[i].span, "declared private here");
        }
        HirExpr *node = make_expr(HirExpr::Kind::Field, expr->span, info.fields[i].type);
        node->left = base;
        node->field = i;
        node->target = type->decl;
        return node;
    }

    Diagnostic &diagnostic = diagnostics_.error(
        Code::UnknownField,
        types_.describe(type) + " has no field '" + interner_.text(expr->name) + "'");
    diagnostic.label(expr->span);
    if (!info.fields.empty()) {
        std::string available;
        for (std::size_t i = 0; i < info.fields.size(); ++i) {
            if (i) available += ", ";
            available += interner_.text(info.fields[i].name);
        }
        diagnostic.note("available fields: " + available);
    }
    return poison(expr->span);
}

HirExpr *Checker::check_index(const Expr *expr) {
    HirExpr *base = check_expr(expr->left, nullptr);
    HirExpr *index = check_expr(expr->right, types_.int_type());

    if (index->type && index->type->kind != TypeKind::Int && !index->type->is_error()) {
        diagnostics_
            .error(Code::IndexNotInteger,
                   "an index must be an int, found " + types_.describe(index->type))
            .label(expr->right->span);
    }

    const Type *type = base->type;
    if (!type || type->is_error()) return poison(expr->span);

    if (type->kind == TypeKind::Nums) {
        HirExpr *node = make_expr(HirExpr::Kind::Index, expr->span, types_.int_type());
        node->left = base;
        node->right = index;
        return node;
    }
    if (type->kind == TypeKind::Slice) {
        HirExpr *node = make_expr(HirExpr::Kind::Index, expr->span, type->element);
        node->left = base;
        node->right = index;
        return node;
    }

    diagnostics_
        .error(Code::NotIndexable, types_.describe(type) + " cannot be indexed")
        .label(expr->span)
        .with_help("indexing works on `nums` and `Slice<int>`");
    return poison(expr->span);
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

HirExpr *Checker::check_unary(const Expr *expr, const Type *expected) {
    // `&x`, `&mut x`, and `&raw x` need the operand's place, not its value.
    if (expr->unary_op == UnaryOp::Ref || expr->unary_op == UnaryOp::MutRef ||
        expr->unary_op == UnaryOp::RawRef) {
        HirExpr *operand = check_expr(expr->left, nullptr);
        const bool wants_mutable = (expr->unary_op == UnaryOp::MutRef);

        if (expr->unary_op == UnaryOp::RawRef && unsafe_depth_ == 0) {
            diagnostics_
                .error(Code::UnsafeRequired, "creating a raw pointer requires an unsafe block")
                .label(expr->span)
                .with_help("wrap it in `unsafe { ... }`");
        }

        // Borrow bookkeeping: an exclusive borrow needs a mutable binding, and
        // shared and exclusive borrows cannot be live at the same time.
        if (expr->left->kind == Expr::Kind::Name) {
            if (LocalBinding *binding = lookup_local(expr->left->name)) {
                if (wants_mutable && !binding->is_mutable) {
                    diagnostics_
                        .error(Code::NotMutable,
                               "cannot borrow '" + interner_.text(expr->left->name) +
                                   "' mutably because it is not declared mutable")
                        .label(expr->span)
                        .with_help("declare it with `let mut` or `keep`");
                }
                if (wants_mutable && binding->shared_borrows > 0) {
                    diagnostics_
                        .error(Code::BorrowConflict,
                               "cannot borrow '" + interner_.text(expr->left->name) +
                                   "' mutably while it is already borrowed")
                        .label(expr->span);
                }
                if (!wants_mutable && binding->mutable_borrows > 0) {
                    diagnostics_
                        .error(Code::BorrowConflict,
                               "cannot borrow '" + interner_.text(expr->left->name) +
                                   "' while it is mutably borrowed")
                        .label(expr->span);
                }
                if (wants_mutable) ++binding->mutable_borrows;
                else ++binding->shared_borrows;
            }
        }

        const Type *type = expr->unary_op == UnaryOp::RawRef
                               ? types_.raw_pointer(operand->type)
                               : types_.reference(operand->type, wants_mutable);
        HirExpr *node = make_expr(HirExpr::Kind::Ref, expr->span, type);
        node->left = operand;
        node->mutable_ref = wants_mutable;
        return node;
    }

    if (expr->unary_op == UnaryOp::Deref) {
        HirExpr *operand = check_expr(expr->left, nullptr);
        const Type *type = operand->type;
        if (!type || type->is_error()) return poison(expr->span);
        if (!type->is_pointer_like()) {
            diagnostics_
                .error(Code::DerefNotPointer,
                       "cannot dereference " + types_.describe(type))
                .label(expr->span);
            return poison(expr->span);
        }
        if (type->kind == TypeKind::RawPointer && unsafe_depth_ == 0) {
            diagnostics_
                .error(Code::UnsafeRequired,
                       "dereferencing a raw pointer requires an unsafe block")
                .label(expr->span)
                .with_help("wrap it in `unsafe { ... }`");
        }
        HirExpr *node = make_expr(HirExpr::Kind::Deref, expr->span, type->element);
        node->left = operand;
        return node;
    }

    if (expr->unary_op == UnaryOp::Await) {
        if (!in_async_ && !(function_ && function_->is_entry)) {
            diagnostics_
                .error(Code::AwaitOutsideAsync,
                       "'await' is only valid inside an async function or a launch block")
                .label(expr->span);
        }
        HirExpr *operand = check_expr(expr->left, nullptr);
        const Type *type = operand->type;
        if (type && type->kind != TypeKind::Task && !type->is_error()) {
            diagnostics_
                .error(Code::NotAwaitable, "cannot await " + types_.describe(type))
                .label(expr->span)
                .with_help("only the result of an `async fn` call is awaitable");
            return poison(expr->span);
        }
        HirExpr *node = make_expr(HirExpr::Kind::Await, expr->span,
                                  type && type->element ? type->element : types_.void_type());
        node->left = operand;
        return node;
    }

    HirExpr *operand = check_expr(expr->left, expected);
    const Type *type = operand->type;
    HirExpr *node = make_expr(HirExpr::Kind::Unary, expr->span, type);
    node->unary_op = expr->unary_op;
    node->left = operand;

    switch (expr->unary_op) {
        case UnaryOp::Negate:
            if (type && !type->is_numeric() && !type->is_error()) {
                diagnostics_
                    .error(Code::InvalidOperand,
                           "cannot negate " + types_.describe(type))
                    .label(expr->span);
                node->type = types_.error();
            }
            break;
        case UnaryOp::Not:
            if (type && type->kind != TypeKind::Bool && !type->is_error()) {
                diagnostics_
                    .error(Code::InvalidOperand,
                           "'not' expects a bool, found " + types_.describe(type))
                    .label(expr->span);
            }
            node->type = types_.bool_type();
            break;
        case UnaryOp::BitNot:
            if (type && type->kind != TypeKind::Int && !type->is_error()) {
                diagnostics_
                    .error(Code::InvalidOperand,
                           "'~' expects an int, found " + types_.describe(type))
                    .label(expr->span);
            }
            node->type = types_.int_type();
            break;
        default:
            break;
    }
    return node;
}

HirExpr *Checker::check_binary(const Expr *expr) {
    HirExpr *left = check_expr(expr->left, nullptr);
    // The right operand is checked against the left's type so integer literals
    // in mixed expressions land on the right type without a conversion rule.
    HirExpr *right = check_expr(expr->right, left->type);

    const BinaryOp op = expr->binary_op;
    HirExpr *node = make_expr(HirExpr::Kind::Binary, expr->span, types_.error());
    node->binary_op = op;
    node->left = left;
    node->right = right;

    const Type *lhs = left->type;
    const Type *rhs = right->type;
    if (!lhs || !rhs || lhs->is_error() || rhs->is_error()) return node;

    auto mismatch = [&] {
        diagnostics_
            .error(Code::InvalidOperand,
                   std::string("cannot apply '") + binary_op_spelling(op) + "' to " +
                       types_.describe(lhs) + " and " + types_.describe(rhs))
            .label(expr->span)
            .with_help("PunPun has no implicit conversions; both operands must have the same type");
    };

    switch (op) {
        case BinaryOp::And:
        case BinaryOp::Or:
            if (lhs->kind != TypeKind::Bool || rhs->kind != TypeKind::Bool) {
                mismatch();
                return node;
            }
            node->type = types_.bool_type();
            return node;

        case BinaryOp::Add:
            // `+` also concatenates strings, which is the one overloaded
            // operator in the language.
            if (lhs->kind == TypeKind::Str && rhs->kind == TypeKind::Str) {
                node->type = types_.str_type();
                return node;
            }
            [[fallthrough]];
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
        case BinaryOp::Modulo:
            if (lhs != rhs || !lhs->is_numeric()) {
                mismatch();
                return node;
            }
            if (op == BinaryOp::Modulo && lhs->kind == TypeKind::Float) {
                diagnostics_
                    .error(Code::InvalidOperand, "'%' is not defined for float")
                    .label(expr->span);
                return node;
            }
            node->type = lhs;
            return node;

        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
            // Equality is defined for every scalar, including str and bool.
            if (lhs != rhs) {
                mismatch();
                return node;
            }
            node->type = types_.bool_type();
            return node;

        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual:
            if (lhs != rhs || (!lhs->is_numeric() && lhs->kind != TypeKind::Str)) {
                mismatch();
                return node;
            }
            node->type = types_.bool_type();
            return node;

        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::ShiftLeft:
        case BinaryOp::ShiftRight:
            if (lhs->kind != TypeKind::Int || rhs->kind != TypeKind::Int) {
                mismatch();
                return node;
            }
            node->type = types_.int_type();
            return node;
    }
    return node;
}

}  // namespace ppc
