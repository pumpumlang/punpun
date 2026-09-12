#include "ppc/sema/checker.hpp"

#include <algorithm>
#include <functional>

namespace ppc {


namespace {

/// Appends a binding step to an arm.
void push_bind(HirArm &arm, const HirBinding &binding) {
    HirArmStep step;
    step.kind = HirArmStep::Kind::Bind;
    step.binding = binding;
    arm.steps.push_back(step);
}

/// Appends a test step to an arm. Tests are kept as separate steps rather than
/// being combined with `and`, so each one can short-circuit to the next arm at
/// the exact point where the bindings before it are known valid.
void push_test(HirArm &arm, HirExpr *test) {
    if (!test) return;
    HirArmStep step;
    step.kind = HirArmStep::Kind::Test;
    step.test = test;
    arm.steps.push_back(step);
}

}  // namespace

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void Checker::check_block(const std::vector<Stmt *> &body, std::vector<HirStmt *> &out) {
    push_scope();

    // Remember which locals this scope introduced so drops can be inserted in
    // reverse declaration order at the end.
    const std::size_t scope_index = scopes_.size() - 1;

    for (const Stmt *statement : body) {
        if (diagnostics_.limit_reached()) break;
        if (HirStmt *lowered = check_statement(statement)) out.push_back(lowered);
    }

    // A borrow lives until its binding leaves scope, so releasing it here is
    // what allows the source to be mutated again afterwards. Done before the
    // drops so the release happens even for bindings that need no destructor.
    for (const LocalBinding &binding : scopes_[scope_index]) {
        if (binding.borrows_from == 0xFFFFFFFFu) continue;
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            for (LocalBinding &source : *scope) {
                if (source.slot != binding.borrows_from) continue;
                if (source.shared_borrows > 0) --source.shared_borrows;
            }
        }
    }

    // Deterministic lexical destruction: every still-initialized move-only local
    // is destroyed exactly once, in reverse declaration order, at normal scope
    // exit. Bindings that were moved out of are skipped.
    const std::vector<LocalBinding> &scope = scopes_[scope_index];
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
        if (it->moved || it->maybe_moved) continue;
        if (!types_.needs_drop(it->type)) continue;
        HirStmt *drop = make_stmt(HirStmt::Kind::Drop, it->move_span);
        drop->local = it->slot;
        out.push_back(drop);
    }

    pop_scope();
}

HirStmt *Checker::check_statement(const Stmt *statement) {
    if (!statement) return nullptr;

    switch (statement->kind) {
        case Stmt::Kind::Let: return check_let(statement);
        case Stmt::Kind::Assign: return check_assign(statement);
        case Stmt::Kind::If: return check_if(statement);
        case Stmt::Kind::While: return check_while(statement);
        case Stmt::Kind::For: return check_for(statement);
        case Stmt::Kind::Return: return check_return(statement);

        case Stmt::Kind::Expression: {
            HirStmt *node = make_stmt(HirStmt::Kind::Expression, statement->span);
            node->value = check_expr(statement->value, nullptr);
            return node;
        }
        case Stmt::Kind::Break: {
            if (loop_depth_ == 0) {
                diagnostics_.error(Code::UnexpectedToken, "'break' outside a loop")
                    .label(statement->span);
                return nullptr;
            }
            return make_stmt(HirStmt::Kind::Break, statement->span);
        }
        case Stmt::Kind::Continue: {
            if (loop_depth_ == 0) {
                diagnostics_.error(Code::UnexpectedToken, "'continue' outside a loop")
                    .label(statement->span);
                return nullptr;
            }
            return make_stmt(HirStmt::Kind::Continue, statement->span);
        }
        case Stmt::Kind::Block: {
            HirStmt *node = make_stmt(HirStmt::Kind::Block, statement->span);
            check_block(statement->body, node->body);
            return node;
        }
        case Stmt::Kind::Unsafe: {
            // Raw pointer operations and native calls are permitted only while
            // this depth is nonzero.
            HirStmt *node = make_stmt(HirStmt::Kind::Block, statement->span);
            ++unsafe_depth_;
            check_block(statement->body, node->body);
            --unsafe_depth_;
            return node;
        }
    }
    return nullptr;
}

HirStmt *Checker::check_let(const Stmt *statement) {
    const Type *declared = resolve_type(statement->declared_type, subst_);

    HirExpr *value = nullptr;
    if (statement->value) {
        value = check_expr(statement->value, declared);
        if (declared) {
            expect_type(value->type, declared, statement->value->span, "this binding");
        }
        note_move(statement->value, value);
    }

    const Type *type = declared ? declared : (value ? value->type : types_.error());
    if (!type || type->is_error()) {
        if (!declared && !value) {
            diagnostics_
                .error(Code::CannotInfer,
                       "cannot infer the type of '" + interner_.text(statement->name) + "'")
                .label(statement->span)
                .with_help("add a type annotation or an initializer");
        }
        type = types_.error();
    }
    if (type->kind == TypeKind::Void) {
        diagnostics_
            .error(Code::TypeMismatch,
                   "cannot bind '" + interner_.text(statement->name) + "' to a void value")
            .label(statement->span)
            .with_help("this expression does not produce a value");
        type = types_.error();
    }

    // A `const` binding is immutable and must be initialized; `keep`/`let mut`
    // are the mutable forms.
    const u32 slot = declare_local(statement->name, type,
                                   statement->is_mutable && !statement->is_const, false,
                                   statement->span);

    // A named borrow of a `nums` list freezes that list for the borrow's
    // lifetime. Recording it here is what lets a later `push` or `sort` on the
    // source be rejected while the view is still reachable.
    if (value && value->kind == HirExpr::Kind::CallBuiltin && value->slot != 0) {
        const u32 source = value->slot - 1;
        if (source < function_->locals.size()) {
            for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
                for (LocalBinding &binding : *scope) {
                    if (binding.slot != source) continue;
                    ++binding.shared_borrows;
                    binding.borrow_span = statement->span;
                }
            }
            // Remember the source so the borrow is released at scope exit.
            for (LocalBinding &binding : scopes_.back()) {
                if (binding.slot == slot) binding.borrows_from = source;
            }
        }
    }

    HirStmt *node = make_stmt(HirStmt::Kind::Let, statement->span);
    node->local = slot;
    node->value = value;
    return node;
}

HirStmt *Checker::check_assign(const Stmt *statement) {
    HirExpr *place = nullptr;

    // Assigning a whole new value to a binding is not a use of the old value —
    // it is exactly how a moved-out binding is revived. So a bare name target
    // is resolved directly here instead of through check_expr, which would
    // report a use-after-move. Compound assignment still goes through the
    // normal path below, because `x += 1` really does read `x`.
    if (statement->target && statement->target->kind == Expr::Kind::Name &&
        !statement->is_compound) {
        if (LocalBinding *binding = lookup_local(statement->target->name)) {
            place = make_expr(HirExpr::Kind::Local, statement->target->span, binding->type);
            place->local = binding->slot;
        }
    }
    if (!place) place = check_expr(statement->target, nullptr);

    // Only a local, a field of one, an element, or a dereference is assignable.
    const HirExpr *root = place;
    while (root && (root->kind == HirExpr::Kind::Field || root->kind == HirExpr::Kind::Index)) {
        root = root->left;
    }

    switch (place->kind) {
        case HirExpr::Kind::Local:
        case HirExpr::Kind::Field:
        case HirExpr::Kind::Index:
        case HirExpr::Kind::Deref:
            break;
        default:
            if (!place->type->is_error()) {
                diagnostics_
                    .error(Code::NotMutable, "this expression cannot be assigned to")
                    .label(statement->target->span)
                    .with_help("assign to a variable, a field, or an element");
            }
            return nullptr;
    }

    if (root && root->kind == HirExpr::Kind::Local) {
        const HirLocal &local = function_->locals[root->local];
        const Type *root_type = root->type;
        const bool whole_binding = (place->kind == HirExpr::Kind::Local);

        // Writing to a field or element through a handle mutates the referent,
        // not the binding, so the binding's own mutability does not govern it.
        // Reassigning the binding itself always does.
        bool through_handle = false;
        if (!whole_binding && root_type) {
            switch (root_type->kind) {
                case TypeKind::Object:      // identity semantics: the handle is shared
                case TypeKind::MutRef:      // an exclusive borrow exists to be written through
                case TypeKind::RawPointer:  // already required an unsafe block
                case TypeKind::Nums:        // a list is a shared handle, like an object
                    through_handle = true;
                    break;
                default:
                    break;
            }
        }

        if (!whole_binding && root_type && root_type->kind == TypeKind::Reference) {
            diagnostics_
                .error(Code::NotMutable, "cannot write through a shared borrow")
                .label(statement->span)
                .secondary(local.span, "'" + interner_.text(local.name) + "' is a `&` borrow")
                .with_help("take the borrow with `&mut` to allow writing");
        } else if (!whole_binding && root_type && root_type->kind == TypeKind::Slice) {
            diagnostics_
                .error(Code::BorrowConflict, "a slice view is read-only")
                .label(statement->span)
                .note("a Slice borrows its source; writing through it would bypass the borrow")
                .with_help("assign into the underlying list instead");
        } else if (!local.is_mutable && !through_handle) {
            diagnostics_
                .error(Code::NotMutable,
                       "cannot assign to '" + interner_.text(local.name) +
                           "' because it is not declared mutable")
                .label(statement->span)
                .secondary(local.span, "declared immutable here")
                .with_help("declare it with `let mut`, or `keep` in the migration dialect");
        }
    }

    // A compound assignment reads the place, applies the operator, and writes
    // back; it is desugared here so nothing downstream sees `+=`.
    HirExpr *value = nullptr;
    if (statement->is_compound) {
        Expr synthetic;
        synthetic.kind = Expr::Kind::Binary;
        synthetic.binary_op = statement->compound_op;
        synthetic.span = statement->span;
        synthetic.left = statement->target;
        synthetic.right = statement->value;
        value = check_binary(&synthetic);
    } else {
        value = check_expr(statement->value, place->type);
        expect_type(value->type, place->type, statement->value->span, "this assignment");
        note_move(statement->value, value);
    }

    // Whole-value assignment reinitializes a moved binding, giving it a new
    // lifetime, which is what makes reuse-after-move legal.
    if (place->kind == HirExpr::Kind::Local) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            for (LocalBinding &binding : *scope) {
                if (binding.slot != place->local) continue;
                binding.moved = false;
                binding.maybe_moved = false;
            }
        }
    }

    HirStmt *node = make_stmt(HirStmt::Kind::Assign, statement->span);
    node->place = place;
    node->value = value;
    return node;
}

HirStmt *Checker::check_if(const Stmt *statement) {
    HirExpr *condition = check_expr(statement->value, types_.bool_type());
    if (condition->type && condition->type->kind != TypeKind::Bool &&
        !condition->type->is_error()) {
        diagnostics_
            .error(Code::ConditionNotBool,
                   "a condition must be bool, found " + types_.describe(condition->type))
            .label(statement->value->span)
            .with_help("compare explicitly, for example `x != 0`");
    }

    HirStmt *node = make_stmt(HirStmt::Kind::If, statement->span);
    node->value = condition;

    // Move state is tracked per branch and merged: a binding moved on only one
    // side becomes "maybe moved" rather than "moved".
    auto snapshot = scopes_;
    check_block(statement->body, node->body);
    auto after_then = scopes_;

    scopes_ = snapshot;
    check_block(statement->alternative, node->alternative);
    auto after_else = scopes_;

    scopes_ = std::move(snapshot);
    for (std::size_t s = 0; s < scopes_.size(); ++s) {
        for (std::size_t b = 0; b < scopes_[s].size(); ++b) {
            LocalBinding &merged = scopes_[s][b];
            if (s >= after_then.size() || b >= after_then[s].size()) continue;
            if (s >= after_else.size() || b >= after_else[s].size()) continue;
            const LocalBinding &lhs = after_then[s][b];
            const LocalBinding &rhs = after_else[s][b];
            const bool lhs_gone = lhs.moved || lhs.maybe_moved;
            const bool rhs_gone = rhs.moved || rhs.maybe_moved;
            if (lhs.moved && rhs.moved) {
                merged.moved = true;
                merged.move_span = lhs.move_span;
            } else if (lhs_gone || rhs_gone) {
                merged.maybe_moved = true;
                merged.move_span = lhs_gone ? lhs.move_span : rhs.move_span;
            }
        }
    }
    return node;
}

HirStmt *Checker::check_while(const Stmt *statement) {
    HirExpr *condition = check_expr(statement->value, types_.bool_type());
    if (condition->type && condition->type->kind != TypeKind::Bool &&
        !condition->type->is_error()) {
        diagnostics_
            .error(Code::ConditionNotBool,
                   "a loop condition must be bool, found " + types_.describe(condition->type))
            .label(statement->value->span);
    }

    HirStmt *node = make_stmt(HirStmt::Kind::While, statement->span);
    node->value = condition;
    ++loop_depth_;
    check_block(statement->body, node->body);
    --loop_depth_;
    return node;
}

HirStmt *Checker::check_for(const Stmt *statement) {
    if (statement->iterable) return check_for_each(statement);

    HirExpr *start = check_expr(statement->range_start, types_.int_type());
    HirExpr *end = check_expr(statement->range_end, types_.int_type());
    expect_type(start->type, types_.int_type(), statement->range_start->span, "a range bound");
    expect_type(end->type, types_.int_type(), statement->range_end->span, "a range bound");

    HirStmt *node = make_stmt(HirStmt::Kind::For, statement->span);
    node->range_start = start;
    node->range_end = end;

    // The induction variable lives in its own scope and is immutable inside the
    // body, matching `for i in 0..n` in the reference implementation.
    push_scope();
    node->local = declare_local(statement->name, types_.int_type(), false, false,
                                statement->span);
    ++loop_depth_;
    check_block(statement->body, node->body);
    --loop_depth_;
    pop_scope();
    return node;
}

/// `for element in sequence` over an owning or borrowed sequence.
///
/// Lowered here rather than in MIR so the three backends keep exactly one loop
/// form to implement. The rewrite is:
///
///     {
///         let __seq = <sequence>;               // evaluated once
///         for __index in 0 .. <length>(__seq) {
///             let element = <element-at>(__seq, __index);
///             <body>
///         }
///     }
///
/// The length is read once per iteration rather than hoisted, which is what
/// makes pushing to the sequence inside the loop behave the way the indexed
/// form it replaces already did.
HirStmt *Checker::check_for_each(const Stmt *statement) {
    HirExpr *sequence = check_expr(statement->iterable, nullptr);
    const Type *type = sequence->type;
    if (!type || type->is_error()) return make_stmt(HirStmt::Kind::Block, statement->span);

    // Built-in sequences keep their compact indexed lowering. User-defined
    // iteration falls through to the structural iterator protocol below.
    const char *length_builtin = nullptr;
    const char *element_builtin = nullptr;
    const Type *element_type = nullptr;
    switch (type->kind) {
        case TypeKind::Nums:
            length_builtin = "size";
            element_type = types_.int_type();
            break;
        case TypeKind::List:
            length_builtin = "list_size";
            element_builtin = "list_at";
            element_type = type->element;
            break;
        case TypeKind::Slice:
            length_builtin = "slice_len";
            element_type = type->element;
            break;
        default:
            break;
    }

    // Every lowering evaluates the source exactly once and keeps it in a
    // compiler-owned local. For move-only user values, entering the loop owns
    // that value just like passing it by value to another function.
    push_scope();
    HirStmt *outer = make_stmt(HirStmt::Kind::Block, statement->span);

    const Symbol sequence_name = interner_.intern("__pp_seq");
    const u32 sequence_local =
        declare_local(sequence_name, type, true, false, statement->span);
    HirStmt *bind_sequence = make_stmt(HirStmt::Kind::Let, statement->span);
    bind_sequence->local = sequence_local;
    bind_sequence->value = sequence;
    outer->body.push_back(bind_sequence);
    note_move(statement->iterable, sequence);

    auto read_local = [&](u32 slot, const Type *local_type, Span span) {
        HirExpr *read = make_expr(HirExpr::Kind::Local, span, local_type);
        read->local = slot;
        return read;
    };

    if (length_builtin) {
        auto read_sequence = [&] {
            return read_local(sequence_local, type, statement->iterable->span);
        };

        HirStmt *loop = make_stmt(HirStmt::Kind::For, statement->span);
        loop->range_start = make_expr(HirExpr::Kind::ConstInt, statement->span,
                                      types_.int_type());
        loop->range_start->int_value = 0;

        HirExpr *length = make_expr(HirExpr::Kind::CallBuiltin, statement->iterable->span,
                                    types_.int_type());
        length->builtin = find_builtin(length_builtin);
        length->operands.push_back(read_sequence());
        loop->range_end = length;

        push_scope();
        const u32 index_local =
            declare_local(interner_.intern("__pp_index"), types_.int_type(), false, false,
                          statement->span);
        loop->local = index_local;

        auto read_index = [&] {
            return read_local(index_local, types_.int_type(), statement->span);
        };

        // nums and Slice index directly; List reads through its accessor builtin.
        HirExpr *element = nullptr;
        if (element_builtin) {
            element = make_expr(HirExpr::Kind::CallBuiltin, statement->span, element_type);
            element->builtin = find_builtin(element_builtin);
            element->operands.push_back(read_sequence());
            element->operands.push_back(read_index());
        } else {
            element = make_expr(HirExpr::Kind::Index, statement->span, element_type);
            element->left = read_sequence();
            element->right = read_index();
        }

        HirStmt *bind_element = make_stmt(HirStmt::Kind::Let, statement->span);
        bind_element->local =
            declare_local(statement->name, element_type, false, false, statement->span);
        bind_element->value = element;
        loop->body.push_back(bind_element);

        ++loop_depth_;
        check_block(statement->body, loop->body);
        --loop_depth_;
        pop_scope();

        outer->body.push_back(loop);
        if (types_.needs_drop(type)) {
            HirStmt *drop = make_stmt(HirStmt::Kind::Drop, statement->span);
            drop->local = sequence_local;
            outer->body.push_back(drop);
        }
        pop_scope();
        return outer;
    }

    // Structural protocol -------------------------------------------------
    //
    // Any aggregate can participate without declaring a compiler-known trait:
    //
    //     value.iter() -> Iterator
    //     iterator.advance() -> Option<T>
    //
    // An iterator may be used directly when it has advance() itself. Protocol
    // methods take no explicit arguments; the element type is recovered from
    // Option<T>. This keeps the protocol useful for generic concrete iterator
    // types without making generic contracts part of the language ABI.
    auto aggregate_type = [&](const Type *candidate) -> const Type * {
        while (candidate && candidate->is_pointer_like() && candidate->element) {
            candidate = candidate->element;
        }
        if (!candidate || !candidate->is_aggregate() || candidate->kind == TypeKind::Enum) {
            return nullptr;
        }
        return candidate;
    };

    auto protocol_method = [&](const Type *receiver, const char *method) -> u32 {
        const Type *owner = aggregate_type(receiver);
        if (!owner || owner->decl >= types_.struct_count()) return 0xFFFFFFFFu;
        const StructInfo &info = types_.struct_at(owner->decl);
        const std::string key = interner_.text(info.name) + "::" + method;
        auto found = by_name_.find(key);
        if (found == by_name_.end()) return 0xFFFFFFFFu;
        u32 match = 0xFFFFFFFFu;
        for (u32 candidate : found->second) {
            if (candidate >= templates_.size()) continue;
            const FunctionTemplate &templ = templates_[candidate];
            if (!templ.decl || !templ.decl->params.empty()) continue;
            // A protocol method cannot have method-level generic parameters:
            // there are no call arguments from which to infer them. Owner type
            // parameters are already concrete on `owner` and are fine.
            if (!templ.decl->generics.empty()) continue;
            if (match != 0xFFFFFFFFu) return 0xFFFFFFFEu;  // ambiguous
            match = candidate;
        }
        return match;
    };

    auto call_protocol_method = [&](u32 templ_index, u32 receiver_slot,
                                    const Type *receiver_type) -> HirExpr * {
        if (templ_index >= templates_.size()) return poison(statement->span);
        const FunctionTemplate &templ = templates_[templ_index];
        const Type *owner = aggregate_type(receiver_type);
        if (!owner) return poison(statement->span);

        // C represents a value struct inline, while the native and bytecode
        // backends currently represent one as an aggregate handle. Taking
        // &mut of the synthetic iterator local would therefore mean different
        // things across backends. Keep the protocol backend-equivalent by
        // requiring stateful iterators to be identity objects for now.
        if (owner->kind == TypeKind::Struct && templ.decl->self_mutable) {
            diagnostics_
                .error(Code::FeatureUnsupported,
                       "a stateful iterator must be an object, not a mutable value struct")
                .label(statement->iterable->span)
                .note("mutable struct receivers are not yet representation-equivalent across "
                      "all PunPun backends")
                .with_help("make the iterator an `object`, or return an object iterator from "
                           "iter()");
            return poison(statement->span);
        }

        std::vector<const Type *> arguments = owner->arguments;
        if (templ.generics.size() != arguments.size()) {
            diagnostics_
                .error(Code::FeatureUnsupported,
                       "iterator protocol methods cannot require inferred type arguments")
                .label(statement->span)
                .with_help("put generic parameters on the iterator type, not on iter() or advance()");
            return poison(statement->span);
        }
        if (!satisfies_constraints(templ, arguments, statement->span)) {
            return poison(statement->span);
        }

        const u32 specialization = specialize(templ_index, arguments, statement->span);
        if (specialization == 0xFFFFFFFFu) return poison(statement->span);
        const Specialization &spec = spec_at(specialization);

        HirExpr *self = read_local(receiver_slot, receiver_type, statement->span);
        if (owner->kind == TypeKind::Struct && templ.decl->self_mutable) {
            // The synthetic local belongs exclusively to the loop lowering, so
            // taking this internal mutable borrow cannot conflict with source
            // code. Building HIR directly also avoids extending a temporary
            // borrow in the source-level borrow checker.
            HirExpr *borrow = make_expr(HirExpr::Kind::Ref, statement->span,
                                        types_.reference(owner, true));
            borrow->left = self;
            borrow->mutable_ref = true;
            self = borrow;
        }

        HirExpr *call = make_expr(HirExpr::Kind::Call, statement->span, spec.result);
        call->target = specialization;
        call->operands.push_back(self);
        return call;
    };

    const u32 iter_method = protocol_method(type, "iter");
    const u32 self_advance_method = protocol_method(type, "advance");
    if (iter_method == 0xFFFFFFFEu || self_advance_method == 0xFFFFFFFEu) {
        diagnostics_
            .error(Code::AmbiguousOverload,
                   "iterator protocol methods must not be overloaded")
            .label(statement->iterable->span)
            .with_help("provide one zero-argument iter() or advance() method");
        pop_scope();
        return outer;
    }

    u32 iterator_local = sequence_local;
    const Type *iterator_type = type;
    bool separate_iterator = false;

    if (iter_method != 0xFFFFFFFFu) {
        HirExpr *iterator = call_protocol_method(iter_method, sequence_local, type);
        if (!iterator || !iterator->type || iterator->type->is_error()) {
            pop_scope();
            return outer;
        }
        iterator_type = iterator->type;
        iterator_local = declare_local(interner_.intern("__pp_iter"), iterator_type, true, false,
                                       statement->span);
        HirStmt *bind_iterator = make_stmt(HirStmt::Kind::Let, statement->span);
        bind_iterator->local = iterator_local;
        bind_iterator->value = iterator;
        outer->body.push_back(bind_iterator);
        separate_iterator = true;
    } else if (self_advance_method == 0xFFFFFFFFu) {
        diagnostics_
            .error(Code::FeatureUnsupported,
                   types_.describe(type) + " cannot be iterated")
            .label(statement->iterable->span)
            .note("custom iteration is structural: iter() returns an iterator whose "
                  "advance() returns Option<T>")
            .with_help("add `fn iter() -> YourIterator`, or make the value itself expose "
                       "`fn advance() -> Option<T>`");
        pop_scope();
        return outer;
    }

    const u32 advance_method = protocol_method(iterator_type, "advance");
    if (advance_method == 0xFFFFFFFFu || advance_method == 0xFFFFFFFEu) {
        diagnostics_
            .error(Code::FeatureUnsupported,
                   "iterator type " + types_.describe(iterator_type) +
                       " must provide one zero-argument advance() method")
            .label(statement->iterable->span)
            .with_help("define `fn advance() -> Option<T>` on the iterator type");
        pop_scope();
        return outer;
    }

    HirExpr *probe_advance = call_protocol_method(advance_method, iterator_local, iterator_type);
    const Type *option_type = probe_advance ? probe_advance->type : nullptr;
    if (!option_type || option_type->is_error()) {
        pop_scope();
        return outer;
    }
    if (option_type->kind != TypeKind::Enum || option_type->decl >= types_.enum_count()) {
        diagnostics_
            .error(Code::FeatureUnsupported, "iterator advance() must return Option<T>")
            .label(statement->iterable->span)
            .note("found " + types_.describe(option_type));
        pop_scope();
        return outer;
    }

    const EnumInfo &option_info = types_.enum_at(option_type->decl);
    if (interner_.text(option_info.name) != "Option") {
        diagnostics_
            .error(Code::FeatureUnsupported, "iterator advance() must return Option<T>")
            .label(statement->iterable->span)
            .note("found " + types_.describe(option_type));
        pop_scope();
        return outer;
    }

    u32 none_variant = 0xFFFFFFFFu;
    u32 some_variant = 0xFFFFFFFFu;
    const Type *protocol_element = nullptr;
    for (u32 i = 0; i < option_info.variants.size(); ++i) {
        const VariantInfo &variant = option_info.variants[i];
        const std::string name = interner_.text(variant.name);
        if (name == "None" && variant.payload.empty()) none_variant = i;
        if (name == "Some" && variant.payload.size() == 1) {
            some_variant = i;
            protocol_element = variant.payload[0];
        }
    }
    if (none_variant == 0xFFFFFFFFu || some_variant == 0xFFFFFFFFu || !protocol_element) {
        diagnostics_
            .error(Code::FeatureUnsupported,
                   "iterator advance() must return the standard Option<T> shape")
            .label(statement->iterable->span);
        pop_scope();
        return outer;
    }

    // The probe above exists only to discover the result type. Rebuild the call
    // for the loop body; otherwise that expression would be shared between HIR
    // locations and could be lowered twice by later transformations.
    HirStmt *loop = make_stmt(HirStmt::Kind::While, statement->span);
    loop->value = make_expr(HirExpr::Kind::ConstBool, statement->span, types_.bool_type());
    loop->value->bool_value = true;

    push_scope();
    const u32 next_local = declare_local(interner_.intern("__pp_next"), option_type, true, false,
                                         statement->span);
    HirStmt *bind_next = make_stmt(HirStmt::Kind::Let, statement->span);
    bind_next->local = next_local;
    bind_next->value = call_protocol_method(advance_method, iterator_local, iterator_type);
    loop->body.push_back(bind_next);

    auto read_next = [&] {
        return read_local(next_local, option_type, statement->span);
    };

    HirExpr *tag = make_expr(HirExpr::Kind::EnumTag, statement->span, types_.int_type());
    tag->left = read_next();
    HirExpr *wanted = make_expr(HirExpr::Kind::ConstInt, statement->span, types_.int_type());
    wanted->int_value = static_cast<i64>(some_variant);
    HirExpr *has_value = make_expr(HirExpr::Kind::Binary, statement->span, types_.bool_type());
    has_value->binary_op = BinaryOp::Equal;
    has_value->left = tag;
    has_value->right = wanted;

    HirStmt *branch = make_stmt(HirStmt::Kind::If, statement->span);
    branch->value = has_value;

    push_scope();
    HirExpr *payload = make_expr(HirExpr::Kind::EnumPayload, statement->span, protocol_element);
    payload->left = read_next();
    payload->field = some_variant;
    payload->slot = 0;

    HirStmt *bind_element = make_stmt(HirStmt::Kind::Let, statement->span);
    bind_element->local =
        declare_local(statement->name, protocol_element, false, false, statement->span);
    bind_element->value = payload;
    branch->body.push_back(bind_element);

    ++loop_depth_;
    check_block(statement->body, branch->body);
    --loop_depth_;
    pop_scope();

    HirStmt *stop = make_stmt(HirStmt::Kind::Break, statement->span);
    branch->alternative.push_back(stop);
    loop->body.push_back(branch);
    pop_scope();

    outer->body.push_back(loop);
    if (separate_iterator && types_.needs_drop(iterator_type)) {
        HirStmt *drop = make_stmt(HirStmt::Kind::Drop, statement->span);
        drop->local = iterator_local;
        outer->body.push_back(drop);
    }
    if (types_.needs_drop(type)) {
        HirStmt *drop = make_stmt(HirStmt::Kind::Drop, statement->span);
        drop->local = sequence_local;
        outer->body.push_back(drop);
    }

    pop_scope();
    return outer;
}

HirStmt *Checker::check_return(const Stmt *statement) {
    HirStmt *node = make_stmt(HirStmt::Kind::Return, statement->span);

    if (!statement->value) {
        if (result_type_ && result_type_->kind != TypeKind::Void) {
            diagnostics_
                .error(Code::MissingReturn,
                       "this function returns " + types_.describe(result_type_) +
                           ", so `return` needs a value")
                .label(statement->span);
        }
        return node;
    }

    node->value = check_expr(statement->value, result_type_);
    if (result_type_ && result_type_->kind == TypeKind::Void) {
        diagnostics_
            .error(Code::TypeMismatch, "this function returns void, so `return` takes no value")
            .label(statement->value->span);
    } else {
        expect_type(node->value->type, result_type_, statement->value->span, "this return");
    }
    note_move(statement->value, node->value);

    // A borrow cannot outlive its owner, so returning a view of a local list is
    // rejected here rather than producing a dangling slice at runtime.
    if (node->value->kind == HirExpr::Kind::CallBuiltin && node->value->slot != 0) {
        diagnostics_
            .error(Code::BorrowEscapes,
                   "cannot return a slice that borrows a local list")
            .label(statement->value->span, "this view borrows a value owned by this function")
            .note("the list is destroyed when the function returns")
            .with_help("return the list itself, or copy the elements you need");
    }
    return node;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

bool Checker::lower_pattern(const Pattern *pattern, const Type *scrutinee, HirArm &arm,
                            std::vector<u32> &introduced) {
    if (!pattern || !scrutinee) return false;

    switch (pattern->kind) {
        case Pattern::Kind::Wildcard:
            arm.is_default = true;
            return true;

        case Pattern::Kind::Binding: {
            // A bare name binds the whole scrutinee and matches everything.
            arm.is_default = true;
            const u32 slot = declare_local(pattern->name, scrutinee, false, false, pattern->span);
            introduced.push_back(slot);
            HirBinding binding;
            binding.local = slot;
            binding.source = HirBinding::kScrutinee;
            binding.payload_slot = HirBinding::kWholeValue;
            push_bind(arm, binding);
            return true;
        }

        case Pattern::Kind::Int:
            if (scrutinee->kind != TypeKind::Int && !scrutinee->is_error()) {
                diagnostics_
                    .error(Code::PatternTypeMismatch,
                           "an integer pattern cannot match " + types_.describe(scrutinee))
                    .label(pattern->span);
                return false;
            }
            arm.tag = pattern->int_value;
            return true;

        case Pattern::Kind::Bool:
            if (scrutinee->kind != TypeKind::Bool && !scrutinee->is_error()) {
                diagnostics_
                    .error(Code::PatternTypeMismatch,
                           "a boolean pattern cannot match " + types_.describe(scrutinee))
                    .label(pattern->span);
                return false;
            }
            arm.tag = pattern->bool_value ? 1 : 0;
            return true;

        case Pattern::Kind::String: {
            if (scrutinee->kind != TypeKind::Str && !scrutinee->is_error()) {
                diagnostics_
                    .error(Code::PatternTypeMismatch,
                           "a string pattern cannot match " + types_.describe(scrutinee))
                    .label(pattern->span);
                return false;
            }
            // Text has no discriminant, so the arm carries an explicit
            // comparison against the scrutinee instead of a tag.
            arm.is_default = true;
            HirExpr *literal = make_expr(HirExpr::Kind::ConstStr, pattern->span, types_.str_type());
            literal->string_value = pattern->string_value;
            HirExpr *subject = make_expr(HirExpr::Kind::MatchSubject, pattern->span, scrutinee);
            HirExpr *compare = make_expr(HirExpr::Kind::Binary, pattern->span, types_.bool_type());
            compare->binary_op = BinaryOp::Equal;
            compare->left = subject;
            compare->right = literal;
            push_test(arm, compare);
            return true;
        }

        case Pattern::Kind::Variant: {
            if (scrutinee->kind != TypeKind::Enum) {
                if (!scrutinee->is_error()) {
                    diagnostics_
                        .error(Code::PatternTypeMismatch,
                               "a variant pattern cannot match " + types_.describe(scrutinee))
                        .label(pattern->span);
                }
                return false;
            }
            const EnumInfo &info = types_.enum_at(scrutinee->decl);
            if (pattern->enum_name.valid() && pattern->enum_name != info.name) {
                diagnostics_
                    .error(Code::PatternTypeMismatch,
                           "pattern names enum '" + interner_.text(pattern->enum_name) +
                               "' but the value is " + types_.describe(scrutinee))
                    .label(pattern->span);
                return false;
            }

            for (u32 i = 0; i < info.variants.size(); ++i) {
                if (info.variants[i].name != pattern->name) continue;
                arm.tag = i;

                const VariantInfo &variant = info.variants[i];
                if (pattern->children.size() != variant.payload.size()) {
                    diagnostics_
                        .error(Code::PatternArity,
                               "variant '" + interner_.text(info.name) + "::" +
                                   interner_.text(variant.name) + "' has " +
                                   std::to_string(variant.payload.size()) +
                                   " payload value(s) but the pattern binds " +
                                   std::to_string(pattern->children.size()))
                        .label(pattern->span);
                    return false;
                }

                // Payload patterns become slot projections. A direct name binds
                // its slot; a nested test binds a temporary and then chains a
                // second projection off that temporary.
                for (std::size_t child = 0; child < pattern->children.size(); ++child) {
                    const Pattern *sub = pattern->children[child];
                    const Type *payload_type = variant.payload[child];

                    if (sub->kind == Pattern::Kind::Wildcard) continue;

                    if (sub->kind == Pattern::Kind::Binding) {
                        const u32 slot =
                            declare_local(sub->name, payload_type, false, false, sub->span);
                        introduced.push_back(slot);
                        HirBinding binding;
                        binding.local = slot;
                        binding.source = HirBinding::kScrutinee;
                        binding.variant = i;
                        binding.payload_slot = static_cast<u32>(child);
                        push_bind(arm, binding);
                        continue;
                    }

                    // Bind the payload to a hidden temporary so the nested
                    // pattern has something to project from and test against.
                    const u32 temp = declare_local(interner_.intern("$payload"), payload_type,
                                                   false, false, sub->span);
                    introduced.push_back(temp);
                    HirBinding binding;
                    binding.local = temp;
                    binding.source = HirBinding::kScrutinee;
                    binding.variant = i;
                    binding.payload_slot = static_cast<u32>(child);
                    push_bind(arm, binding);

                    HirArm nested;
                    if (!lower_pattern(sub, payload_type, nested, introduced)) return false;

                    // The nested arm's own discriminant test must run before any
                    // of its bindings, because those bindings project payload
                    // slots that only exist on the variant this test confirms.
                    // Emitting it here — after the temporary is bound, before
                    // the nested steps are appended — is what keeps
                    // `Held(Some(v))` from reading a payload slot out of a
                    // `None` value.
                    if (!nested.is_default) {
                        HirExpr *read = make_expr(HirExpr::Kind::Local, sub->span, payload_type);
                        read->local = temp;
                        HirExpr *tag =
                            make_expr(HirExpr::Kind::EnumTag, sub->span, types_.int_type());
                        tag->left = read;
                        HirExpr *want =
                            make_expr(HirExpr::Kind::ConstInt, sub->span, types_.int_type());
                        want->int_value = nested.tag;
                        HirExpr *condition =
                            make_expr(HirExpr::Kind::Binary, sub->span, types_.bool_type());
                        condition->binary_op = BinaryOp::Equal;
                        condition->left = tag;
                        condition->right = want;
                        push_test(arm, condition);
                    }

                    // The nested arm described itself relative to its own
                    // scrutinee, which is this temporary. Re-anchor its steps
                    // and splice them in, preserving their relative order.
                    for (HirArmStep &step : nested.steps) {
                        if (step.kind == HirArmStep::Kind::Bind) {
                            if (step.binding.source == HirBinding::kScrutinee) {
                                step.binding.source = temp;
                            }
                        } else if (step.test) {
                            // A nested test referred to its own subject; rewrite
                            // that to read the temporary.
                            std::function<void(HirExpr *)> reanchor = [&](HirExpr *node) {
                                if (!node) return;
                                if (node->kind == HirExpr::Kind::MatchSubject) {
                                    node->kind = HirExpr::Kind::Local;
                                    node->local = temp;
                                }
                                reanchor(node->left);
                                reanchor(node->right);
                                for (HirExpr *operand : node->operands) reanchor(operand);
                            };
                            reanchor(step.test);
                        }
                        arm.steps.push_back(step);
                    }
                }
                return true;
            }

            diagnostics_
                .error(Code::UnknownVariant,
                       "enum '" + interner_.text(info.name) + "' has no variant '" +
                           interner_.text(pattern->name) + "'")
                .label(pattern->span);
            return false;
        }
    }
    return false;
}

std::vector<const Type *> Checker::constructor_fields(const Pattern *pattern,
                                                      const Type *column) {
    // Specializing the matrix on a constructor replaces its column with the
    // constructor's payload columns. Literals and wildcards contribute none.
    if (!pattern || !column) return {};
    if (pattern->kind != Pattern::Kind::Variant) return {};
    if (column->kind != TypeKind::Enum || column->decl >= types_.enum_count()) return {};

    const EnumInfo &info = types_.enum_at(column->decl);
    for (const VariantInfo &variant : info.variants) {
        if (variant.name == pattern->name) return variant.payload;
    }
    return {};
}

bool Checker::find_missing_value(const PatternMatrix &matrix,
                                 const std::vector<const Type *> &columns,
                                 std::string &witness) {
    // With no columns left, the empty tuple is covered exactly when some row
    // survived to this point.
    if (columns.empty()) return matrix.empty();

    const Type *column = columns[0];
    const std::vector<const Type *> rest(columns.begin() + 1, columns.end());

    auto is_wildcard = [](const Pattern *pattern) {
        return !pattern || pattern->kind == Pattern::Kind::Wildcard ||
               pattern->kind == Pattern::Kind::Binding;
    };

    // Specialize on one constructor: rows that match it keep going, with their
    // sub-patterns spliced into the front.
    auto specialize_on = [&](const Pattern *representative,
                             const std::vector<const Type *> &fields) {
        PatternMatrix specialized;
        for (const PatternRow &row : matrix) {
            const Pattern *head = row[0];
            PatternRow next;

            if (is_wildcard(head)) {
                // A wildcard matches every constructor, contributing wildcards
                // for each of its fields.
                next.assign(fields.size(), nullptr);
            } else if (head->kind == Pattern::Kind::Variant) {
                if (!representative || representative->kind != Pattern::Kind::Variant) continue;
                if (head->name != representative->name) continue;
                for (const Pattern *child : head->children) next.push_back(child);
                // A pattern that bound fewer children than the variant has was
                // already reported; pad so the row width stays consistent.
                while (next.size() < fields.size()) next.push_back(nullptr);
            } else {
                continue;  // a literal never matches a variant constructor
            }

            next.insert(next.end(), row.begin() + 1, row.end());
            specialized.push_back(std::move(next));
        }
        return specialized;
    };

    // The default matrix keeps only the rows whose head matches anything.
    auto default_matrix = [&] {
        PatternMatrix defaulted;
        for (const PatternRow &row : matrix) {
            if (!is_wildcard(row[0])) continue;
            defaulted.emplace_back(row.begin() + 1, row.end());
        }
        return defaulted;
    };

    // --- enums: a finite, known set of constructors ------------------------
    if (column && column->kind == TypeKind::Enum && column->decl < types_.enum_count()) {
        const EnumInfo &info = types_.enum_at(column->decl);

        std::vector<bool> present(info.variants.size(), false);
        std::vector<const Pattern *> representative(info.variants.size(), nullptr);
        for (const PatternRow &row : matrix) {
            const Pattern *head = row[0];
            if (is_wildcard(head) || head->kind != Pattern::Kind::Variant) continue;
            for (std::size_t i = 0; i < info.variants.size(); ++i) {
                if (info.variants[i].name != head->name) continue;
                present[i] = true;
                if (!representative[i]) representative[i] = head;
            }
        }

        const bool complete =
            std::all_of(present.begin(), present.end(), [](bool value) { return value; });

        if (!complete) {
            // Some variant is never named. Recurse on the default matrix; if
            // the remaining columns admit a gap, name a missing variant.
            std::string tail;
            if (!find_missing_value(default_matrix(), rest, tail)) return false;
            for (std::size_t i = 0; i < info.variants.size(); ++i) {
                if (present[i]) continue;
                witness = interner_.text(info.name) + "::" + interner_.text(info.variants[i].name);
                if (!info.variants[i].payload.empty()) witness += "(..)";
                if (!tail.empty()) witness += ", " + tail;
                return true;
            }
            return false;
        }

        // Every variant appears, so a gap must live inside one of them.
        for (std::size_t i = 0; i < info.variants.size(); ++i) {
            const std::vector<const Type *> &fields = info.variants[i].payload;
            std::vector<const Type *> nested = fields;
            nested.insert(nested.end(), rest.begin(), rest.end());

            std::string tail;
            if (!find_missing_value(specialize_on(representative[i], fields), nested, tail)) {
                continue;
            }
            witness = interner_.text(info.name) + "::" + interner_.text(info.variants[i].name);
            if (!fields.empty()) {
                // The witness for the payload columns comes back comma
                // separated; the leading `fields.size()` entries belong here.
                witness += "(" + (tail.empty() ? std::string("..") : tail) + ")";
            } else if (!tail.empty()) {
                witness += ", " + tail;
            }
            return true;
        }
        return false;
    }

    // --- bool: exactly two constructors ------------------------------------
    if (column && column->kind == TypeKind::Bool) {
        bool has_true = false;
        bool has_false = false;
        for (const PatternRow &row : matrix) {
            const Pattern *head = row[0];
            if (is_wildcard(head) || head->kind != Pattern::Kind::Bool) continue;
            (head->bool_value ? has_true : has_false) = true;
        }

        if (!has_true || !has_false) {
            std::string tail;
            if (!find_missing_value(default_matrix(), rest, tail)) return false;
            witness = has_true ? "false" : "true";
            if (!tail.empty()) witness += ", " + tail;
            return true;
        }

        for (bool value : {false, true}) {
            PatternMatrix specialized;
            for (const PatternRow &row : matrix) {
                const Pattern *head = row[0];
                if (!is_wildcard(head) &&
                    (head->kind != Pattern::Kind::Bool || head->bool_value != value)) {
                    continue;
                }
                specialized.emplace_back(row.begin() + 1, row.end());
            }
            std::string tail;
            if (!find_missing_value(specialized, rest, tail)) continue;
            witness = value ? "true" : "false";
            if (!tail.empty()) witness += ", " + tail;
            return true;
        }
        return false;
    }

    // --- open domains: int, str, and everything else -----------------------
    // These can never be covered by listing constructors, so only a wildcard
    // row makes them exhaustive.
    std::string tail;
    if (!find_missing_value(default_matrix(), rest, tail)) return false;
    witness = "_";
    if (!tail.empty()) witness += ", " + tail;
    return true;
}

void Checker::check_exhaustiveness(const Expr *expr, const Type *scrutinee,
                                   const std::vector<const Pattern *> &patterns) {
    if (!scrutinee || scrutinee->is_error()) return;

    PatternMatrix matrix;
    matrix.reserve(patterns.size());
    for (const Pattern *pattern : patterns) matrix.push_back(PatternRow{pattern});

    std::string witness;
    if (!find_missing_value(matrix, {scrutinee}, witness)) return;

    Diagnostic &diagnostic = diagnostics_.error(
        Code::NonExhaustiveMatch,
        "this match does not cover every value of " + types_.describe(scrutinee));
    diagnostic.label(expr->span, "not covered: " + witness);

    if (scrutinee->kind == TypeKind::Int || scrutinee->kind == TypeKind::Str) {
        diagnostic.note("the set of possible " + types_.describe(scrutinee) +
                        " values is unbounded");
    }
    diagnostic.with_help("add the missing arm(s), or a `_ =>` wildcard");
}

HirExpr *Checker::check_match(const Expr *expr, const Type *expected) {
    HirExpr *scrutinee = check_expr(expr->left, nullptr);
    const Type *scrutinee_type = scrutinee->type;

    HirExpr *node = make_expr(HirExpr::Kind::Match, expr->span, types_.error());
    node->left = scrutinee;

    const Type *result = expected;
    /// Unguarded patterns accepted so far. Guarded arms are excluded because a
    /// guard can fail, so they never contribute coverage.
    std::vector<const Pattern *> covering;

    for (const MatchArm &arm : expr->arms) {
        HirArm lowered;
        lowered.span = arm.span;

        // An arm is unreachable when everything it could match is already
        // matched by the arms above it, which is the same question
        // exhaustiveness asks about the whole match.
        if (!covering.empty() && scrutinee_type && !scrutinee_type->is_error()) {
            PatternMatrix preceding;
            for (const Pattern *pattern : covering) preceding.push_back(PatternRow{pattern});
            std::string unused;
            if (!find_missing_value(preceding, {scrutinee_type}, unused)) {
                diagnostics_.warning(Code::UnreachableArm, "this arm is unreachable")
                    .label(arm.span, "the arms above already match every value")
                    .secondary(expr->arms.front().span, "matching starts here");
            }
        }

        push_scope();
        std::vector<u32> introduced;
        const bool ok = lower_pattern(arm.pattern, scrutinee_type, lowered, introduced);

        if (ok && !arm.guard) covering.push_back(arm.pattern);

        if (arm.guard) {
            HirExpr *guard = check_expr(arm.guard, types_.bool_type());
            if (guard->type && guard->type->kind != TypeKind::Bool &&
                !guard->type->is_error()) {
                diagnostics_
                    .error(Code::ConditionNotBool, "a match guard must be bool")
                    .label(arm.guard->span);
            }
            // The guard runs last: it may read anything the pattern bound.
            push_test(lowered, guard);
        }

        if (arm.has_block) {
            check_block(arm.body, lowered.body);
        } else {
            lowered.value = check_expr(arm.value, result);
            // The first arm fixes the match's type; the rest are checked
            // against it, which is what makes `match` usable as an expression.
            if (!result && lowered.value->type && !lowered.value->type->is_error()) {
                result = lowered.value->type;
            } else if (result) {
                expect_type(lowered.value->type, result, arm.value->span, "this match arm");
            }
        }
        pop_scope();

        if (ok) node->arms.push_back(std::move(lowered));
    }

    node->type = result ? result : types_.void_type();
    check_exhaustiveness(expr, scrutinee_type, covering);
    return node;
}

// ---------------------------------------------------------------------------
// Error propagation
// ---------------------------------------------------------------------------

HirExpr *Checker::check_propagate(const Expr *expr) {
    HirExpr *operand = check_expr(expr->left, nullptr);
    const Type *type = operand->type;
    if (!type || type->is_error()) return poison(expr->span);

    if (type->kind != TypeKind::Enum) {
        diagnostics_
            .error(Code::PropagateTypeMismatch,
                   "'?' expects an Option or Result, found " + types_.describe(type))
            .label(expr->span);
        return poison(expr->span);
    }

    const EnumInfo &info = types_.enum_at(type->decl);
    const std::string name = interner_.text(info.name);
    if (name != "Option" && name != "Result") {
        diagnostics_
            .error(Code::PropagateTypeMismatch,
                   "'?' works on Option and Result, not " + types_.describe(type))
            .label(expr->span);
        return poison(expr->span);
    }

    // The enclosing function must return the same outer enum. `?` never
    // converts between Option and Result.
    if (!result_type_ || result_type_->kind != TypeKind::Enum) {
        diagnostics_
            .error(Code::PropagateTypeMismatch,
                   "'?' requires the enclosing function to return " + name)
            .label(expr->span)
            .note("this function returns " +
                  (result_type_ ? types_.describe(result_type_) : std::string("void")))
            .with_help("change the return type, or handle the value with `match`");
        return poison(expr->span);
    }
    const EnumInfo &outer = types_.enum_at(result_type_->decl);
    if (interner_.text(outer.name) != name) {
        diagnostics_
            .error(Code::PropagateTypeMismatch,
                   "'?' cannot convert " + types_.describe(type) + " into " +
                       types_.describe(result_type_))
            .label(expr->span)
            .note("PunPun never converts implicitly between Option and Result")
            .with_help("match on the value and construct the outer type yourself");
        return poison(expr->span);
    }

    // For Result the error payloads must match, since the failing branch
    // forwards the error value unchanged.
    if (name == "Result") {
        const Type *from_error = type->arguments.size() > 1 ? type->arguments[1] : nullptr;
        const Type *to_error =
            result_type_->arguments.size() > 1 ? result_type_->arguments[1] : nullptr;
        if (from_error && to_error && from_error != to_error) {
            diagnostics_
                .error(Code::PropagateTypeMismatch,
                       "'?' cannot forward error type " + types_.describe(from_error) +
                           " into a function returning error type " + types_.describe(to_error))
                .label(expr->span)
                .with_help("make the error types match, or convert explicitly");
        }
    }

    // Desugars to: match value { Ok(v) => v, Error(e) => return Error(e) }.
    // Building it here keeps `?` out of every later phase.
    const u32 success_variant = (name == "Option") ? 1 : 0;
    const u32 failure_variant = (name == "Option") ? 0 : 1;
    const Type *payload = info.variants[success_variant].payload.empty()
                              ? types_.void_type()
                              : info.variants[success_variant].payload[0];

    HirExpr *node = make_expr(HirExpr::Kind::Match, expr->span, payload);
    node->left = operand;

    push_scope();
    {
        HirArm ok;
        ok.span = expr->span;
        ok.tag = success_variant;
        const u32 slot = declare_local(interner_.intern("$ok"), payload, false, false, expr->span);
        HirBinding binding;
        binding.local = slot;
        binding.source = HirBinding::kScrutinee;
        binding.variant = success_variant;
        binding.payload_slot = 0;
        push_bind(ok, binding);
        HirExpr *read = make_expr(HirExpr::Kind::Local, expr->span, payload);
        read->local = slot;
        ok.value = read;
        node->arms.push_back(std::move(ok));
    }
    {
        HirArm fail;
        fail.span = expr->span;
        fail.tag = failure_variant;
        fail.is_default = true;  // the remaining variant

        HirStmt *ret = make_stmt(HirStmt::Kind::Return, expr->span);
        if (name == "Option") {
            HirExpr *none = make_expr(HirExpr::Kind::MakeEnum, expr->span, result_type_);
            none->target = result_type_->decl;
            none->field = 0;
            ret->value = none;
        } else {
            const Type *error_type =
                type->arguments.size() > 1 ? type->arguments[1] : types_.error();
            const u32 slot =
                declare_local(interner_.intern("$err"), error_type, false, false, expr->span);
            HirBinding binding;
            binding.local = slot;
            binding.source = HirBinding::kScrutinee;
            binding.variant = failure_variant;
            binding.payload_slot = 0;
            push_bind(fail, binding);
            HirExpr *read = make_expr(HirExpr::Kind::Local, expr->span, error_type);
            read->local = slot;
            HirExpr *wrapped = make_expr(HirExpr::Kind::MakeEnum, expr->span, result_type_);
            wrapped->target = result_type_->decl;
            wrapped->field = 1;
            wrapped->operands.push_back(read);
            ret->value = wrapped;
        }
        fail.body.push_back(ret);
        node->arms.push_back(std::move(fail));
    }
    pop_scope();
    return node;
}

}  // namespace ppc
