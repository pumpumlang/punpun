#include "ppc/mir/builder.hpp"

namespace ppc {

// ---------------------------------------------------------------------------
// Block plumbing
// ---------------------------------------------------------------------------

MirInst &MirBuilder::emit(MirOp op, Span span) {
    // Once a block is terminated, anything else in it is unreachable. Rather
    // than tracking dead code everywhere, emission goes to a scratch block that
    // compute_cfg will mark unreachable and the DCE pass will delete.
    if (current_ == kNoBlock) current_ = function_->add_block();
    MirBlock &block = function_->blocks[current_];
    block.instructions.emplace_back();
    MirInst &instruction = block.instructions.back();
    instruction.op = op;
    instruction.span = span;
    return instruction;
}

void MirBuilder::emit_jump(BlockId target, Span span) {
    if (terminated()) return;
    MirInst &instruction = emit(MirOp::Jump, span);
    instruction.then_block = target;
    current_ = kNoBlock;
}

void MirBuilder::emit_branch(Reg condition, BlockId then_block, BlockId else_block, Span span) {
    if (terminated()) return;
    MirInst &instruction = emit(MirOp::Branch, span);
    instruction.a = condition;
    instruction.then_block = then_block;
    instruction.else_block = else_block;
    current_ = kNoBlock;
}

Reg MirBuilder::const_int(i64 value, Span span) {
    MirInst &instruction = emit(MirOp::ConstInt, span);
    instruction.dest = function_->add_reg(types_.int_type());
    instruction.imm = value;
    instruction.type = types_.int_type();
    return instruction.dest;
}

Reg MirBuilder::const_bool(bool value, Span span) {
    MirInst &instruction = emit(MirOp::ConstBool, span);
    instruction.dest = function_->add_reg(types_.bool_type());
    instruction.imm = value ? 1 : 0;
    instruction.type = types_.bool_type();
    return instruction.dest;
}

u32 MirBuilder::new_temp_local(const Type *type) {
    MirLocal local;
    local.type = type;
    function_->locals.push_back(local);
    return static_cast<u32>(function_->locals.size() - 1);
}

void MirBuilder::mark_address_taken(const HirExpr *expr) {
    // Walk to the root of a place expression; if it bottoms out at a local,
    // that local can no longer be kept purely in a register.
    while (expr && (expr->kind == HirExpr::Kind::Field ||
                    expr->kind == HirExpr::Kind::Index)) {
        expr = expr->left;
    }
    if (expr && expr->kind == HirExpr::Kind::Local &&
        expr->local < function_->locals.size()) {
        function_->locals[expr->local].address_taken = true;
    }
}

// ---------------------------------------------------------------------------
// Program and function lowering
// ---------------------------------------------------------------------------

MirProgram *MirBuilder::build(const HirProgram &program) {
    program_ = arena_.make<MirProgram>();
    program_->entry = program.entry;
    program_->injections = program.injections;

    for (const HirFunction *source : program.functions) {
        MirFunction *fn = arena_.make<MirFunction>();
        fn->name = source->name;
        fn->span = source->span;
        fn->result = source->result;
        fn->is_entry = source->is_entry;
        fn->is_async = source->is_async;
        fn->is_extern_native = source->is_extern_native;
        fn->native_symbol = source->native_symbol;
        program_->functions.push_back(fn);
    }

    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const HirFunction *source = program.functions[i];
        if (source->is_extern_native) continue;
        lower_function(*source, *program_->functions[i]);
    }
    return program_;
}

void MirBuilder::lower_function(const HirFunction &source, MirFunction &out) {
    function_ = &out;
    loops_.clear();
    match_subjects_.clear();

    for (const HirLocal &local : source.locals) {
        MirLocal lowered;
        lowered.name = local.name;
        lowered.type = local.type;
        lowered.is_parameter = local.is_parameter;
        out.locals.push_back(lowered);
    }
    out.param_count = source.param_count;
    for (const HirCapture &capture : source.captures) {
        MirCapture lowered;
        lowered.local = capture.local;
        lowered.type = capture.type;
        lowered.is_mutable = capture.is_mutable;
        out.captures.push_back(lowered);
    }

    out.entry_block = out.add_block();
    current_ = out.entry_block;

    // Materialize the closure environment into normal locals once on entry.
    // The body then needs no special name-resolution path for captures.
    for (std::size_t i = 0; i < out.captures.size(); ++i) {
        const MirCapture &capture = out.captures[i];
        MirInst &load = emit(MirOp::LoadCapture, source.span);
        const Reg captured = out.add_reg(capture.type);
        load.dest = captured;
        load.index = static_cast<u32>(i);
        load.type = capture.type;
        MirInst &store = emit(MirOp::StoreLocal, source.span);
        store.index = capture.local;
        store.a = captured;
    }

    lower_block(source.body);

    // A function that runs off the end returns implicitly. For a non-void
    // function the checker already reported a missing return, so this only has
    // to keep the CFG well formed.
    if (!terminated()) {
        flush_captures(source.span);
        MirInst &instruction = emit(MirOp::Return, source.span);
        instruction.a = kNoReg;
        current_ = kNoBlock;
    }

    out.compute_cfg();
    function_ = nullptr;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void MirBuilder::lower_block(const std::vector<HirStmt *> &body) {
    for (const HirStmt *statement : body) lower_stmt(statement);
}

void MirBuilder::lower_stmt(const HirStmt *statement) {
    if (!statement) return;

    switch (statement->kind) {
        case HirStmt::Kind::Let: {
            if (statement->value) {
                const Reg value = lower_expr(statement->value);
                MirInst &instruction = emit(MirOp::StoreLocal, statement->span);
                instruction.index = statement->local;
                instruction.a = value;
            }
            return;
        }
        case HirStmt::Kind::Assign: {
            const Reg value = lower_expr(statement->value);
            lower_assign(statement->place, value, statement->span);
            return;
        }
        case HirStmt::Kind::Expression: {
            // The result is discarded, but the expression may still have
            // effects, so it is lowered rather than skipped.
            lower_expr(statement->value);
            return;
        }
        case HirStmt::Kind::Return: {
            const Reg value = statement->value ? lower_expr(statement->value) : kNoReg;
            flush_captures(statement->span);
            MirInst &instruction = emit(MirOp::Return, statement->span);
            instruction.a = value;
            current_ = kNoBlock;
            return;
        }
        case HirStmt::Kind::If: lower_if(statement); return;
        case HirStmt::Kind::While: lower_while(statement); return;
        case HirStmt::Kind::For: lower_for(statement); return;
        case HirStmt::Kind::Break: {
            if (loops_.empty()) return;
            emit_jump(loops_.back().break_target, statement->span);
            return;
        }
        case HirStmt::Kind::Continue: {
            if (loops_.empty()) return;
            emit_jump(loops_.back().continue_target, statement->span);
            return;
        }
        case HirStmt::Kind::Block: lower_block(statement->body); return;
        case HirStmt::Kind::Drop: {
            MirInst &instruction = emit(MirOp::Drop, statement->span);
            instruction.index = statement->local;
            return;
        }
    }
}

void MirBuilder::lower_if(const HirStmt *statement) {
    const Reg condition = lower_expr(statement->value);

    const BlockId then_block = function_->add_block();
    const bool has_else = !statement->alternative.empty();
    const BlockId else_block = has_else ? function_->add_block() : kNoBlock;
    const BlockId join = function_->add_block();

    emit_branch(condition, then_block, has_else ? else_block : join, statement->span);

    start_block(then_block);
    lower_block(statement->body);
    emit_jump(join, statement->span);

    if (has_else) {
        start_block(else_block);
        lower_block(statement->alternative);
        emit_jump(join, statement->span);
    }

    start_block(join);
}

void MirBuilder::lower_while(const HirStmt *statement) {
    // The header is a separate block so `continue` has somewhere to jump that
    // re-evaluates the condition.
    const BlockId header = function_->add_block();
    const BlockId body = function_->add_block();
    const BlockId exit = function_->add_block();

    emit_jump(header, statement->span);

    start_block(header);
    const Reg condition = lower_expr(statement->value);
    emit_branch(condition, body, exit, statement->span);

    start_block(body);
    loops_.push_back(LoopFrame{header, exit});
    lower_block(statement->body);
    loops_.pop_back();
    emit_jump(header, statement->span);

    start_block(exit);
}

void MirBuilder::lower_for(const HirStmt *statement) {
    // `for i in start..end` lowers to an explicit counter. The upper bound is
    // evaluated once, before the loop, so a call in the bound does not run on
    // every iteration.
    const Reg start = lower_expr(statement->range_start);
    {
        MirInst &instruction = emit(MirOp::StoreLocal, statement->span);
        instruction.index = statement->local;
        instruction.a = start;
    }

    const Reg limit = lower_expr(statement->range_end);
    const u32 limit_slot = new_temp_local(types_.int_type());
    {
        MirInst &instruction = emit(MirOp::StoreLocal, statement->span);
        instruction.index = limit_slot;
        instruction.a = limit;
    }

    const BlockId header = function_->add_block();
    const BlockId body = function_->add_block();
    const BlockId latch = function_->add_block();
    const BlockId exit = function_->add_block();

    emit_jump(header, statement->span);

    start_block(header);
    Reg counter;
    {
        MirInst &instruction = emit(MirOp::LoadLocal, statement->span);
        instruction.dest = function_->add_reg(types_.int_type());
        instruction.index = statement->local;
        instruction.type = types_.int_type();
        counter = instruction.dest;
    }
    Reg bound;
    {
        MirInst &instruction = emit(MirOp::LoadLocal, statement->span);
        instruction.dest = function_->add_reg(types_.int_type());
        instruction.index = limit_slot;
        instruction.type = types_.int_type();
        bound = instruction.dest;
    }
    Reg condition;
    {
        MirInst &instruction = emit(MirOp::Binary, statement->span);
        instruction.dest = function_->add_reg(types_.bool_type());
        instruction.binary_op = BinaryOp::Less;
        instruction.a = counter;
        instruction.b = bound;
        instruction.type = types_.bool_type();
        condition = instruction.dest;
    }
    emit_branch(condition, body, exit, statement->span);

    start_block(body);
    // `continue` goes to the latch, not the header, so the increment still runs.
    loops_.push_back(LoopFrame{latch, exit});
    lower_block(statement->body);
    loops_.pop_back();
    emit_jump(latch, statement->span);

    start_block(latch);
    Reg current;
    {
        MirInst &instruction = emit(MirOp::LoadLocal, statement->span);
        instruction.dest = function_->add_reg(types_.int_type());
        instruction.index = statement->local;
        instruction.type = types_.int_type();
        current = instruction.dest;
    }
    const Reg one = const_int(1, statement->span);
    Reg next;
    {
        MirInst &instruction = emit(MirOp::Binary, statement->span);
        instruction.dest = function_->add_reg(types_.int_type());
        instruction.binary_op = BinaryOp::Add;
        instruction.a = current;
        instruction.b = one;
        instruction.type = types_.int_type();
        next = instruction.dest;
    }
    {
        MirInst &instruction = emit(MirOp::StoreLocal, statement->span);
        instruction.index = statement->local;
        instruction.a = next;
    }
    emit_jump(header, statement->span);

    start_block(exit);
}

void MirBuilder::flush_captures(Span span) {
    if (!function_) return;
    for (std::size_t i = 0; i < function_->captures.size(); ++i) {
        const MirCapture &capture = function_->captures[i];
        if (!capture.is_mutable) continue;
        MirInst &load = emit(MirOp::LoadLocal, span);
        const Reg value = function_->add_reg(capture.type);
        load.dest = value;
        load.index = capture.local;
        load.type = capture.type;
        MirInst &store = emit(MirOp::StoreCapture, span);
        store.index = static_cast<u32>(i);
        store.a = value;
        store.type = capture.type;
    }
}

// ---------------------------------------------------------------------------
// Assignment places
// ---------------------------------------------------------------------------

void MirBuilder::lower_assign(const HirExpr *place, Reg value, Span span) {
    if (!place) return;

    switch (place->kind) {
        case HirExpr::Kind::Local: {
            MirInst &instruction = emit(MirOp::StoreLocal, span);
            instruction.index = place->local;
            instruction.a = value;
            return;
        }
        case HirExpr::Kind::Deref: {
            const Reg pointer = lower_expr(place->left);
            MirInst &instruction = emit(MirOp::StoreDeref, span);
            instruction.a = pointer;
            instruction.b = value;
            return;
        }
        case HirExpr::Kind::Index: {
            const Reg base = lower_expr(place->left);
            const Reg index = lower_expr(place->right);
            MirInst &instruction = emit(MirOp::SetIndex, span);
            instruction.a = base;
            instruction.b = index;
            instruction.c = value;
            return;
        }
        case HirExpr::Kind::Field: {
            const Type *base_type = place->left ? place->left->type : nullptr;
            const Reg base = lower_expr(place->left);
            {
                MirInst &instruction = emit(MirOp::SetField, span);
                instruction.a = base;
                instruction.index = place->field;
                instruction.b = value;
                instruction.target = place->target;
            }
            // An object is a handle, so writing through it is enough. A value
            // struct was copied into `base`, so the mutated copy has to be
            // written back to wherever it came from. That recursion is what
            // makes `a.b.c = x` update `a`.
            if (base_type && base_type->kind != TypeKind::Object &&
                !base_type->is_pointer_like()) {
                lower_assign(place->left, base, span);
            }
            return;
        }
        default:
            // The checker rejects everything else, so reaching here means a
            // bug in this compiler rather than in the user's program.
            diagnostics_
                .error(Code::BackendInternal, "cannot lower this assignment target")
                .label(span)
                .note("this is a compiler bug; please report it");
            return;
    }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

Reg MirBuilder::lower_expr(const HirExpr *expr) {
    if (!expr) return kNoReg;

    switch (expr->kind) {
        case HirExpr::Kind::ConstInt: {
            MirInst &instruction = emit(MirOp::ConstInt, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.imm = expr->int_value;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::ConstFloat: {
            MirInst &instruction = emit(MirOp::ConstFloat, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.fimm = expr->float_value;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::ConstStr: {
            MirInst &instruction = emit(MirOp::ConstStr, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.text = expr->string_value;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::ConstBool: {
            MirInst &instruction = emit(MirOp::ConstBool, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.imm = expr->bool_value ? 1 : 0;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Local: {
            MirInst &instruction = emit(MirOp::LoadLocal, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.index = expr->local;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::MatchSubject: {
            // Substituted with the register holding the innermost scrutinee.
            if (match_subjects_.empty()) return kNoReg;
            return match_subjects_.back();
        }
        case HirExpr::Kind::Binary: return lower_binary(expr);

        case HirExpr::Kind::Unary: {
            const Reg operand = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::Unary, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.unary_op = expr->unary_op;
            instruction.a = operand;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Call:
        case HirExpr::Kind::CallBuiltin: return lower_call(expr);
        case HirExpr::Kind::FuncRef: {
            std::vector<Reg> captures;
            captures.reserve(expr->operands.size());
            for (const HirExpr *capture : expr->operands) captures.push_back(lower_expr(capture));
            MirInst &instruction = emit(MirOp::MakeClosure, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.target = expr->target;
            instruction.args = std::move(captures);
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::CallIndirect: {
            const Reg callee = lower_expr(expr->left);
            std::vector<Reg> arguments;
            arguments.reserve(expr->operands.size());
            for (const HirExpr *operand : expr->operands) {
                arguments.push_back(lower_expr(operand));
            }
            MirInst &instruction = emit(MirOp::CallIndirect, expr->span);
            if (expr->type && expr->type->kind != TypeKind::Void) {
                instruction.dest = function_->add_reg(expr->type);
            }
            instruction.a = callee;
            instruction.args = std::move(arguments);
            instruction.type = expr->type;
            // The callee's static type is the signature; the backends use it to
            // pick the right calling sequence.
            instruction.callee_type = expr->left ? expr->left->type : nullptr;
            return instruction.dest;
        }

        case HirExpr::Kind::Field: {
            const Reg base = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::GetField, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.a = base;
            instruction.index = expr->field;
            instruction.target = expr->target;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Index: {
            const Reg base = lower_expr(expr->left);
            const Reg index = lower_expr(expr->right);
            MirInst &instruction = emit(MirOp::GetIndex, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.a = base;
            instruction.b = index;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::MakeStruct: {
            std::vector<Reg> fields;
            fields.reserve(expr->operands.size());
            for (const HirExpr *operand : expr->operands) fields.push_back(lower_expr(operand));
            MirInst &instruction = emit(MirOp::MakeStruct, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.target = expr->target;
            instruction.args = std::move(fields);
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::MakeEnum: {
            std::vector<Reg> payload;
            payload.reserve(expr->operands.size());
            for (const HirExpr *operand : expr->operands) payload.push_back(lower_expr(operand));
            MirInst &instruction = emit(MirOp::MakeEnum, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.target = expr->target;
            instruction.index = expr->field;
            instruction.args = std::move(payload);
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::EnumTag: {
            const Reg operand = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::EnumTag, expr->span);
            instruction.dest = function_->add_reg(types_.int_type());
            instruction.a = operand;
            instruction.type = types_.int_type();
            return instruction.dest;
        }
        case HirExpr::Kind::EnumPayload: {
            const Reg operand = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::EnumPayload, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.a = operand;
            instruction.index = expr->field;
            instruction.slot = expr->slot;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::ListLiteral: {
            std::vector<Reg> elements;
            elements.reserve(expr->operands.size());
            for (const HirExpr *operand : expr->operands) elements.push_back(lower_expr(operand));
            MirInst &instruction = emit(MirOp::MakeList, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.args = std::move(elements);
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Ref: {
            mark_address_taken(expr->left);
            // A borrow of a local becomes the local's address; a borrow of a
            // field or element borrows the aggregate it lives in.
            const HirExpr *root = expr->left;
            while (root && (root->kind == HirExpr::Kind::Field ||
                            root->kind == HirExpr::Kind::Index)) {
                root = root->left;
            }
            if (root && root->kind == HirExpr::Kind::Local) {
                MirInst &instruction = emit(MirOp::LocalAddr, expr->span);
                instruction.dest = function_->add_reg(expr->type);
                instruction.index = root->local;
                instruction.type = expr->type;
                return instruction.dest;
            }
            return lower_expr(expr->left);
        }
        case HirExpr::Kind::Deref: {
            const Reg pointer = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::Deref, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.a = pointer;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Await: {
            const Reg task = lower_expr(expr->left);
            MirInst &instruction = emit(MirOp::Await, expr->span);
            instruction.dest = function_->add_reg(expr->type);
            instruction.a = task;
            instruction.type = expr->type;
            return instruction.dest;
        }
        case HirExpr::Kind::Match: return lower_match(expr);

        case HirExpr::Kind::Block: {
            lower_block(expr->body);
            return expr->left ? lower_expr(expr->left) : kNoReg;
        }
    }
    return kNoReg;
}

Reg MirBuilder::lower_binary(const HirExpr *expr) {
    // `and` and `or` must not evaluate their right operand unless needed, so
    // they become branches rather than a single instruction.
    if (expr->binary_op == BinaryOp::And || expr->binary_op == BinaryOp::Or) {
        return lower_short_circuit(expr);
    }

    const Reg left = lower_expr(expr->left);
    const Reg right = lower_expr(expr->right);
    MirInst &instruction = emit(MirOp::Binary, expr->span);
    instruction.dest = function_->add_reg(expr->type);
    instruction.binary_op = expr->binary_op;
    instruction.a = left;
    instruction.b = right;
    instruction.type = expr->type;
    // The operand type decides which runtime helper a backend picks, and the
    // result type of a comparison is bool, so it cannot be recovered later.
    instruction.imm = expr->left && expr->left->type
                          ? static_cast<i64>(expr->left->type->kind)
                          : static_cast<i64>(TypeKind::Error);
    return instruction.dest;
}

Reg MirBuilder::lower_short_circuit(const HirExpr *expr) {
    const bool is_and = (expr->binary_op == BinaryOp::And);
    const u32 slot = new_temp_local(types_.bool_type());

    const Reg left = lower_expr(expr->left);
    {
        MirInst &instruction = emit(MirOp::StoreLocal, expr->span);
        instruction.index = slot;
        instruction.a = left;
    }

    const BlockId evaluate = function_->add_block();
    const BlockId join = function_->add_block();

    // `a and b` evaluates b only when a is true; `a or b` only when a is false.
    if (is_and) {
        emit_branch(left, evaluate, join, expr->span);
    } else {
        emit_branch(left, join, evaluate, expr->span);
    }

    start_block(evaluate);
    const Reg right = lower_expr(expr->right);
    {
        MirInst &instruction = emit(MirOp::StoreLocal, expr->span);
        instruction.index = slot;
        instruction.a = right;
    }
    emit_jump(join, expr->span);

    start_block(join);
    MirInst &instruction = emit(MirOp::LoadLocal, expr->span);
    instruction.dest = function_->add_reg(types_.bool_type());
    instruction.index = slot;
    instruction.type = types_.bool_type();
    return instruction.dest;
}

Reg MirBuilder::lower_call(const HirExpr *expr) {
    std::vector<Reg> arguments;
    arguments.reserve(expr->operands.size());
    for (const HirExpr *operand : expr->operands) arguments.push_back(lower_expr(operand));

    const bool builtin = (expr->kind == HirExpr::Kind::CallBuiltin);
    MirInst &instruction = emit(builtin ? MirOp::CallBuiltin : MirOp::Call, expr->span);
    // A void call still gets a destination register in the C backend's eyes only
    // if the type is non-void; otherwise the result is dropped.
    if (expr->type && expr->type->kind != TypeKind::Void) {
        instruction.dest = function_->add_reg(expr->type);
    }
    instruction.target = builtin ? expr->builtin : expr->target;
    instruction.args = std::move(arguments);
    instruction.type = expr->type;
    return instruction.dest;
}

// ---------------------------------------------------------------------------
// Match
// ---------------------------------------------------------------------------

Reg MirBuilder::lower_match(const HirExpr *expr) {
    const Reg subject = lower_expr(expr->left);
    const Type *subject_type = expr->left ? expr->left->type : nullptr;

    // The scrutinee is stashed in a local so every arm can re-read it without
    // re-evaluating the expression, and so nested tests can refer to it.
    const u32 subject_slot = new_temp_local(subject_type);
    {
        MirInst &instruction = emit(MirOp::StoreLocal, expr->span);
        instruction.index = subject_slot;
        instruction.a = subject;
    }

    const bool produces_value = expr->type && expr->type->kind != TypeKind::Void &&
                                !expr->type->is_error();
    const u32 result_slot = produces_value ? new_temp_local(expr->type) : 0xFFFFFFFFu;

    // Enum matches test a discriminant; bool and int matches test the value.
    const bool tag_based = subject_type && subject_type->kind == TypeKind::Enum;

    const BlockId join = function_->add_block();

    for (std::size_t i = 0; i < expr->arms.size(); ++i) {
        const HirArm &arm = expr->arms[i];
        const bool last = (i + 1 == expr->arms.size());

        const BlockId body = function_->add_block();
        // A failed arm falls through to the next test. After the last arm the
        // match is exhaustive by construction, so control goes to the join.
        const BlockId next = last ? join : function_->add_block();

        // Step 1: the tag or value test, when this arm has one.
        if (!arm.is_default) {
            Reg reloaded;
            {
                MirInst &instruction = emit(MirOp::LoadLocal, arm.span);
                instruction.dest = function_->add_reg(subject_type);
                instruction.index = subject_slot;
                instruction.type = subject_type;
                reloaded = instruction.dest;
            }
            Reg observed = reloaded;
            if (tag_based) {
                MirInst &instruction = emit(MirOp::EnumTag, arm.span);
                instruction.dest = function_->add_reg(types_.int_type());
                instruction.a = reloaded;
                instruction.type = types_.int_type();
                observed = instruction.dest;
            }
            const Reg wanted = tag_based || (subject_type &&
                                             subject_type->kind == TypeKind::Int)
                                   ? const_int(arm.tag, arm.span)
                                   : const_bool(arm.tag != 0, arm.span);
            Reg matches;
            {
                MirInst &instruction = emit(MirOp::Binary, arm.span);
                instruction.dest = function_->add_reg(types_.bool_type());
                instruction.binary_op = BinaryOp::Equal;
                instruction.a = observed;
                instruction.b = wanted;
                instruction.type = types_.bool_type();
                instruction.imm = tag_based
                                      ? static_cast<i64>(TypeKind::Int)
                                      : static_cast<i64>(subject_type ? subject_type->kind
                                                                      : TypeKind::Int);
                matches = instruction.dest;
            }
            // Bindings and the extra test live in their own block so they only
            // run once the tag already matched.
            const BlockId bind = function_->add_block();
            emit_branch(matches, bind, next, arm.span);
            start_block(bind);
        }

        // Step 2: walk the arm's tests and bindings in order. A failed test
        // jumps to the next arm immediately, which is what guarantees a payload
        // projection only runs once the test establishing its variant has
        // passed. Doing all the bindings first and testing afterwards would
        // read a payload slot that the actual variant does not have.
        for (const HirArmStep &step : arm.steps) {
            if (step.kind == HirArmStep::Kind::Test) {
                Reg reloaded;
                {
                    MirInst &instruction = emit(MirOp::LoadLocal, arm.span);
                    instruction.dest = function_->add_reg(subject_type);
                    instruction.index = subject_slot;
                    instruction.type = subject_type;
                    reloaded = instruction.dest;
                }
                match_subjects_.push_back(reloaded);
                const Reg condition = lower_expr(step.test);
                match_subjects_.pop_back();

                const BlockId survived = function_->add_block();
                emit_branch(condition, survived, next, arm.span);
                start_block(survived);
                continue;
            }

            const HirBinding &binding = step.binding;
            Reg source;
            {
                MirInst &instruction = emit(MirOp::LoadLocal, arm.span);
                const u32 slot = (binding.source == HirBinding::kScrutinee) ? subject_slot
                                                                            : binding.source;
                instruction.dest = function_->add_reg(function_->locals[slot].type);
                instruction.index = slot;
                instruction.type = function_->locals[slot].type;
                source = instruction.dest;
            }

            Reg value = source;
            if (binding.payload_slot != HirBinding::kWholeValue) {
                MirInst &instruction = emit(MirOp::EnumPayload, arm.span);
                instruction.dest =
                    function_->add_reg(function_->locals[binding.local].type);
                instruction.a = source;
                instruction.index = binding.variant;
                instruction.slot = binding.payload_slot;
                instruction.type = function_->locals[binding.local].type;
                value = instruction.dest;
            }

            MirInst &store = emit(MirOp::StoreLocal, arm.span);
            store.index = binding.local;
            store.a = value;
        }

        emit_jump(body, arm.span);

        // Step 4: the arm body.
        start_block(body);
        if (arm.value) {
            const Reg value = lower_expr(arm.value);
            if (produces_value && value != kNoReg) {
                MirInst &instruction = emit(MirOp::StoreLocal, arm.span);
                instruction.index = result_slot;
                instruction.a = value;
            }
        } else {
            lower_block(arm.body);
        }
        emit_jump(join, arm.span);

        if (!last) start_block(next);
    }

    start_block(join);
    if (!produces_value) return kNoReg;

    MirInst &instruction = emit(MirOp::LoadLocal, expr->span);
    instruction.dest = function_->add_reg(expr->type);
    instruction.index = result_slot;
    instruction.type = expr->type;
    return instruction.dest;
}

}  // namespace ppc
