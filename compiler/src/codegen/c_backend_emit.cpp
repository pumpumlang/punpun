#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "ppc/codegen/c_backend.hpp"
#include "ppc/sema/builtins.hpp"

namespace ppc {

// ---------------------------------------------------------------------------
// Binary operators
// ---------------------------------------------------------------------------

void CBackend::emit_binary(const MirFunction &fn, const MirInst &instruction) {
    const std::string destination = reg(instruction.dest);
    const std::string left = reg(instruction.a);
    const std::string right = reg(instruction.b);

    // The builder stashed the operand's TypeKind in `imm`, because a comparison
    // erases it from the result type.
    const TypeKind operand = static_cast<TypeKind>(instruction.imm);

    auto plain = [&](const char *op) {
        out_ << "    " << destination << " = " << left << " " << op << " " << right << ";\n";
    };
    auto helper = [&](const char *name) {
        out_ << "    " << destination << " = " << name << "(" << left << ", " << right << ");\n";
    };

    switch (instruction.binary_op) {
        case BinaryOp::Add:
            // `+` on text concatenates; on numbers it adds.
            if (operand == TypeKind::Str) { helper("pp_concat"); return; }
            if (operand == TypeKind::Float) { plain("+"); return; }
            options_.unchecked_arithmetic ? plain("+") : helper("pp_add_i64");
            return;
        case BinaryOp::Subtract:
            if (operand == TypeKind::Float) { plain("-"); return; }
            options_.unchecked_arithmetic ? plain("-") : helper("pp_sub_i64");
            return;
        case BinaryOp::Multiply:
            if (operand == TypeKind::Float) { plain("*"); return; }
            options_.unchecked_arithmetic ? plain("*") : helper("pp_mul_i64");
            return;
        case BinaryOp::Divide:
            if (operand == TypeKind::Float) { plain("/"); return; }
            // Even unchecked, integer division by zero is undefined in C, so it
            // always goes through the runtime helper.
            helper("pp_div_i64");
            return;
        case BinaryOp::Modulo:
            helper("pp_mod_i64");
            return;

        case BinaryOp::Equal:
            if (operand == TypeKind::Str) { helper("pp_str_eq"); return; }
            plain("==");
            return;
        case BinaryOp::NotEqual:
            if (operand == TypeKind::Str) {
                out_ << "    " << destination << " = !pp_str_eq(" << left << ", " << right
                     << ");\n";
                return;
            }
            plain("!=");
            return;
        case BinaryOp::Less:
            if (operand == TypeKind::Str) {
                out_ << "    " << destination << " = pp_str_cmp(" << left << ", " << right
                     << ") < 0;\n";
                return;
            }
            plain("<");
            return;
        case BinaryOp::LessEqual:
            if (operand == TypeKind::Str) {
                out_ << "    " << destination << " = pp_str_cmp(" << left << ", " << right
                     << ") <= 0;\n";
                return;
            }
            plain("<=");
            return;
        case BinaryOp::Greater:
            if (operand == TypeKind::Str) {
                out_ << "    " << destination << " = pp_str_cmp(" << left << ", " << right
                     << ") > 0;\n";
                return;
            }
            plain(">");
            return;
        case BinaryOp::GreaterEqual:
            if (operand == TypeKind::Str) {
                out_ << "    " << destination << " = pp_str_cmp(" << left << ", " << right
                     << ") >= 0;\n";
                return;
            }
            plain(">=");
            return;

        case BinaryOp::BitAnd: plain("&"); return;
        case BinaryOp::BitOr: plain("|"); return;
        case BinaryOp::BitXor: plain("^"); return;
        case BinaryOp::ShiftLeft:
            options_.unchecked_arithmetic ? plain("<<") : helper("pp_shl_i64");
            return;
        case BinaryOp::ShiftRight:
            options_.unchecked_arithmetic ? plain(">>") : helper("pp_shr_i64");
            return;

        // The builder turns these into branches, so reaching here means a
        // short-circuit was lowered as a plain operation somewhere.
        case BinaryOp::And: plain("&&"); return;
        case BinaryOp::Or: plain("||"); return;
    }
    (void)fn;
}

// ---------------------------------------------------------------------------
// Builtins
// ---------------------------------------------------------------------------

void CBackend::emit_builtin(const MirFunction &fn, const MirInst &instruction) {
    const std::vector<BuiltinSpec> &table = builtin_table();
    if (instruction.target >= table.size()) {
        diagnostics_.error(Code::BackendInternal, "unknown builtin in MIR")
            .label(instruction.span);
        return;
    }
    const BuiltinSpec &spec = table[instruction.target];
    const std::string name = spec.name;

    auto argument = [&](std::size_t i) {
        return i < instruction.args.size() ? reg(instruction.args[i]) : std::string("0");
    };
    auto argument_type = [&](std::size_t i) -> const Type * {
        return i < instruction.args.size() ? reg_type(fn, instruction.args[i]) : nullptr;
    };

    // print / println / say pick a runtime formatter from the argument's static
    // type, since C has no overloading and the runtime has one entry per type.
    if (name == "print" || name == "println" || name == "say") {
        const bool newline = (name != "print");
        const Type *type = argument_type(0);
        const char *suffix = "str";
        if (type) {
            switch (type->kind) {
                case TypeKind::Int: suffix = "int"; break;
                case TypeKind::Float: suffix = "float"; break;
                case TypeKind::Bool: suffix = "bool"; break;
                case TypeKind::Str: suffix = "str"; break;
                default: suffix = "int"; break;
            }
        }
        out_ << "    pp_" << (newline ? "println_" : "print_") << suffix << "(" << argument(0)
             << ");\n";
        return;
    }

    // `move` is a compile-time transfer; at runtime the value is just copied.
    if (name == "move") {
        if (instruction.dest != kNoReg) {
            out_ << "    " << reg(instruction.dest) << " = " << argument(0) << ";\n";
        }
        return;
    }
    // `drop` ends the binding's lifetime. The runtime releases everything at
    // cleanup, so this only has to be a no-op with a visible marker.
    if (name == "drop") {
        out_ << "    (void)" << argument(0) << ";  /* drop */\n";
        return;
    }

    if (!spec.symbol || !*spec.symbol) {
        diagnostics_
            .error(Code::BackendInternal, "builtin '" + name + "' has no runtime symbol")
            .label(instruction.span);
        return;
    }

    // A List stores 8-byte slots and the runtime is untyped, so values crossing
    // that boundary are reinterpreted rather than converted. The compiler has
    // already checked the element type; this only restores the C type.
    //
    // A value struct or enum is a real C aggregate, not a word, so it cannot be
    // reinterpreted at all. Those are boxed: push copies the value into a fresh
    // block and stores the pointer, and a read copies back out. That keeps value
    // semantics exactly as they are everywhere else — two reads of the same
    // element yield independent values.
    auto is_boxed = [](const Type *type) {
        return type && (type->kind == TypeKind::Struct || type->kind == TypeKind::Enum);
    };

    std::vector<std::string> slot_expression(instruction.args.size());
    for (std::size_t i = 0; i < instruction.args.size(); ++i) {
        const bool is_slot = i < spec.params.size() &&
                             (spec.params[i] == BuiltinType::ListElement ||
                              spec.params[i] == BuiltinType::MapValue);
        if (!is_slot) {
            slot_expression[i] = argument(i);
            continue;
        }
        const Type *type = argument_type(i);
        if (!type) {
            slot_expression[i] = argument(i);
        } else if (is_boxed(type)) {
            // A temporary per call site; the runtime owns the block afterwards.
            const std::string box = "box" + std::to_string(temporary_id_++);
            out_ << "    " << type_name(type) << " *" << box << " = ("
                 << type_name(type) << " *)pp_object_alloc((int64_t)sizeof("
                 << type_name(type) << "));\n";
            out_ << "    *" << box << " = " << argument(i) << ";\n";
            slot_expression[i] = "(int64_t)(intptr_t)" + box;
        } else if (type->kind == TypeKind::Float) {
            // A double's bits, not its integer value: converting would round.
            slot_expression[i] = "pp_bits_from_f64(" + argument(i) + ")";
        } else if (type->kind == TypeKind::Int || type->kind == TypeKind::Bool) {
            slot_expression[i] = argument(i);
        } else {
            slot_expression[i] = "(int64_t)(intptr_t)" + argument(i);
        }
    }

    const bool returns_slot = (spec.result == BuiltinType::ListElement ||
                               spec.result == BuiltinType::MapValue);

    out_ << "    ";
    if (instruction.dest != kNoReg) {
        out_ << reg(instruction.dest) << " = ";
        if (returns_slot && instruction.type) {
            if (is_boxed(instruction.type)) {
                out_ << "*(" << type_name(instruction.type) << " *)(intptr_t)";
            } else if (instruction.type->kind == TypeKind::Float) {
                out_ << "pp_f64_from_bits(";
            } else if (instruction.type->kind != TypeKind::Int &&
                       instruction.type->kind != TypeKind::Bool) {
                out_ << "(" << type_name(instruction.type) << ")(intptr_t)";
            }
        }
    }
    out_ << spec.symbol << "(";
    for (std::size_t i = 0; i < instruction.args.size(); ++i) {
        if (i) out_ << ", ";
        out_ << slot_expression[i];
    }
    out_ << ")";
    if (instruction.dest != kNoReg && returns_slot && instruction.type &&
        !is_boxed(instruction.type) && instruction.type->kind == TypeKind::Float) {
        out_ << ")";
    }
    out_ << ";\n";
}

std::string CBackend::await_call(const Type *result, const std::string &task) const {
    // The runtime returns a task's result as raw bits; the caller picks the
    // accessor that reinterprets them correctly.
    if (!result || result->kind == TypeKind::Void) return "pp_task_await_void(" + task + ")";
    switch (result->kind) {
        case TypeKind::Float: return "pp_task_await_f64(" + task + ")";
        case TypeKind::Int:
        case TypeKind::Bool: return "pp_task_await_i64(" + task + ")";
        default: return "pp_task_await_ptr(" + task + ")";
    }
}

// ---------------------------------------------------------------------------
// Instructions
// ---------------------------------------------------------------------------

void CBackend::emit_instruction(const MirFunction &fn, const MirInst &instruction) {
    switch (instruction.op) {
        case MirOp::ConstInt:
            out_ << "    " << reg(instruction.dest) << " = ";
            if (instruction.imm == INT64_MIN) {
                // C has no negative literals: `-9223372036854775808` is unary
                // minus applied to a value one past INT64_MAX, which does not
                // fit a signed type. Writing it as a subtraction keeps every
                // intermediate in range.
                out_ << "(INT64_C(-9223372036854775807) - 1)";
            } else {
                out_ << "INT64_C(" << instruction.imm << ")";
            }
            out_ << ";\n";
            return;
        case MirOp::ConstBool:
            out_ << "    " << reg(instruction.dest) << " = "
                 << (instruction.imm ? "true" : "false") << ";\n";
            return;
        case MirOp::ConstFloat: {
            // %.17g round-trips an IEEE double exactly.
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.17g", instruction.fimm);
            out_ << "    " << reg(instruction.dest) << " = " << buffer << ";\n";
            return;
        }
        case MirOp::ConstStr:
            out_ << "    " << reg(instruction.dest) << " = "
                 << quote(types_.interner().text(instruction.text)) << ";\n";
            return;

        case MirOp::LoadLocal:
            out_ << "    " << reg(instruction.dest) << " = " << local(instruction.index) << ";\n";
            return;
        case MirOp::StoreLocal:
            out_ << "    " << local(instruction.index) << " = " << reg(instruction.a) << ";\n";
            return;
        case MirOp::LocalAddr:
            out_ << "    " << reg(instruction.dest) << " = &" << local(instruction.index) << ";\n";
            return;

        case MirOp::Binary: emit_binary(fn, instruction); return;

        case MirOp::Unary: {
            const std::string destination = reg(instruction.dest);
            const std::string operand = reg(instruction.a);
            const Type *type = reg_type(fn, instruction.a);
            switch (instruction.unary_op) {
                case UnaryOp::Negate:
                    if (type && type->kind == TypeKind::Float) {
                        out_ << "    " << destination << " = -" << operand << ";\n";
                    } else if (options_.unchecked_arithmetic) {
                        out_ << "    " << destination << " = -" << operand << ";\n";
                    } else {
                        out_ << "    " << destination << " = pp_neg_i64(" << operand << ");\n";
                    }
                    return;
                case UnaryOp::Not:
                    out_ << "    " << destination << " = !" << operand << ";\n";
                    return;
                case UnaryOp::BitNot:
                    out_ << "    " << destination << " = ~" << operand << ";\n";
                    return;
                default:
                    out_ << "    " << destination << " = " << operand << ";\n";
                    return;
            }
        }

        case MirOp::Call: {
            out_ << "    ";
            if (instruction.dest != kNoReg) out_ << reg(instruction.dest) << " = ";

            const MirFunction *target = instruction.target < program_->functions.size()
                                            ? program_->functions[instruction.target]
                                            : nullptr;
            // Calling an async function spawns a task rather than running the
            // body; the trampoline unpacks the arguments on the worker thread.
            if (target && target->is_async) {
                out_ << "pptask_spawn" << instruction.target << "(";
            } else {
                out_ << function_name(instruction.target) << "(";
            }
            for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                if (i) out_ << ", ";
                out_ << reg(instruction.args[i]);
            }
            out_ << ");\n";
            return;
        }
        case MirOp::CallBuiltin: emit_builtin(fn, instruction); return;

        case MirOp::MakeStruct: {
            const Type *type = instruction.type;
            const std::string destination = reg(instruction.dest);
            if (type && type->kind == TypeKind::Object) {
                // Identity objects live on the heap so the handle can be shared.
                out_ << "    " << destination << " = (" << aggregate_name(type)
                     << " *)pp_object_alloc((int64_t)sizeof(" << aggregate_name(type) << "));\n";
                for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                    out_ << "    " << destination << "->f" << i << " = "
                         << reg(instruction.args[i]) << ";\n";
                }
                return;
            }
            // Value structs are plain C aggregates.
            const StructInfo &info = types_.struct_at(type->decl);
            if (instruction.args.empty() && info.fields.empty()) {
                out_ << "    memset(&" << destination << ", 0, sizeof(" << destination << "));\n";
                return;
            }
            for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                out_ << "    " << destination << ".f" << i << " = " << reg(instruction.args[i])
                     << ";\n";
            }
            // Fields with no supplied argument keep whatever the zeroing at
            // declaration left them with.
            return;
        }

        case MirOp::GetField: {
            const Type *base = reg_type(fn, instruction.a);
            out_ << "    " << reg(instruction.dest) << " = " << reg(instruction.a)
                 << access(base) << "f" << instruction.index << ";\n";
            return;
        }
        case MirOp::SetField: {
            const Type *base = reg_type(fn, instruction.a);
            out_ << "    " << reg(instruction.a) << access(base) << "f" << instruction.index
                 << " = " << reg(instruction.b) << ";\n";
            return;
        }

        case MirOp::MakeEnum: {
            const std::string destination = reg(instruction.dest);
            out_ << "    " << destination << ".tag = INT64_C(" << instruction.index << ");\n";
            for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                out_ << "    " << destination << ".as.v" << instruction.index << ".f" << i
                     << " = " << reg(instruction.args[i]) << ";\n";
            }
            return;
        }
        case MirOp::EnumTag:
            out_ << "    " << reg(instruction.dest) << " = " << reg(instruction.a) << ".tag;\n";
            return;
        case MirOp::EnumPayload:
            out_ << "    " << reg(instruction.dest) << " = " << reg(instruction.a) << ".as.v"
                 << instruction.index << ".f" << instruction.slot << ";\n";
            return;

        case MirOp::MakeList: {
            const std::string destination = reg(instruction.dest);
            out_ << "    " << destination << " = pp_numbers_new();\n";
            for (Reg element : instruction.args) {
                out_ << "    pp_push(" << destination << ", " << reg(element) << ");\n";
            }
            return;
        }

        case MirOp::GetIndex: {
            const Type *base = reg_type(fn, instruction.a);
            const char *helper = (base && base->kind == TypeKind::Slice) ? "pp_slice_at_i64"
                                                                        : "pp_at";
            out_ << "    " << reg(instruction.dest) << " = " << helper << "("
                 << reg(instruction.a) << ", " << reg(instruction.b) << ");\n";
            return;
        }
        case MirOp::SetIndex:
            out_ << "    pp_put(" << reg(instruction.a) << ", " << reg(instruction.b) << ", "
                 << reg(instruction.c) << ");\n";
            return;

        case MirOp::Deref:
            out_ << "    " << reg(instruction.dest) << " = *" << reg(instruction.a) << ";\n";
            return;
        case MirOp::StoreDeref:
            out_ << "    *" << reg(instruction.a) << " = " << reg(instruction.b) << ";\n";
            return;

        case MirOp::Await: {
            const std::string call = await_call(instruction.type, reg(instruction.a));
            out_ << "    ";
            if (instruction.dest != kNoReg) out_ << reg(instruction.dest) << " = ";
            else out_ << "(void)";
            out_ << call << ";\n";
            return;
        }

        case MirOp::Drop:
            out_ << "    /* drop " << local(instruction.index) << " */\n";
            return;

        case MirOp::Jump:
            out_ << "    goto bb" << instruction.then_block << ";\n";
            return;
        case MirOp::Branch:
            out_ << "    if (" << reg(instruction.a) << ") goto bb" << instruction.then_block
                 << "; else goto bb" << instruction.else_block << ";\n";
            return;
        case MirOp::Return:
            if (instruction.a != kNoReg) {
                out_ << "    return " << reg(instruction.a) << ";\n";
            } else if (fn.result && fn.result->kind != TypeKind::Void) {
                // The checker already reported the missing return; emit a
                // well-defined value so the C compiler does not also complain.
                out_ << "    return (" << type_name(fn.result) << "){0};\n";
            } else {
                out_ << "    return;\n";
            }
            return;
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

void CBackend::emit_function(const MirFunction &fn, std::size_t index) {
    // An async function's body is an ordinary function; the trampoline emitted
    // separately is what runs it on a worker thread.
    out_ << "/* " << fn.name << " */\n";
    out_ << "static " << type_name(fn.result) << " " << function_name(index) << "(";

    if (fn.param_count == 0) {
        out_ << "void";
    } else {
        for (u32 i = 0; i < fn.param_count; ++i) {
            if (i) out_ << ", ";
            out_ << type_name(fn.locals[i].type) << " p" << i;
        }
    }
    out_ << ") {\n";

    // Locals are declared up front and zero-initialized. The host compiler
    // promotes the ones that stay in registers, which is why ppc's own
    // optimizer does not need phi nodes.
    for (std::size_t i = 0; i < fn.locals.size(); ++i) {
        const Type *type = fn.locals[i].type;
        if (!type || type->kind == TypeKind::Void) continue;
        out_ << "    " << type_name(type) << " " << local(static_cast<u32>(i));
        if (i < fn.param_count) {
            out_ << " = p" << i;
        } else if (type->is_aggregate() && type->kind != TypeKind::Object) {
            out_ << " = {0}";
        } else {
            out_ << " = 0";
        }
        out_ << ";\n";
    }

    for (std::size_t i = 0; i < fn.reg_types.size(); ++i) {
        const Type *type = fn.reg_types[i];
        if (!type || type->kind == TypeKind::Void) continue;
        out_ << "    " << type_name(type) << " " << reg(static_cast<Reg>(i));
        if (type->is_aggregate() && type->kind != TypeKind::Object) out_ << " = {0}";
        else out_ << " = 0";
        out_ << ";\n";
    }

    if (!fn.locals.empty() || !fn.reg_types.empty()) out_ << "\n";

    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        // A label with no statement after it is invalid in C before C23, so
        // every block gets an explicit no-op.
        out_ << "bb" << block.id << ": ;\n";
        for (const MirInst &instruction : block.instructions) {
            emit_instruction(fn, instruction);
        }
    }

    out_ << "}\n\n";
}

void CBackend::emit_prototypes(const MirProgram &program) {
    out_ << "/* ---- forward declarations ---- */\n\n";
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const MirFunction *fn = program.functions[i];
        if (fn->is_extern_native) {
            // Native declarations are provided by an @inject block or a linked
            // object, so they are declared extern rather than defined.
            out_ << "extern " << type_name(fn->result) << " " << fn->native_symbol << "(";
            if (fn->param_count == 0) out_ << "void";
            for (u32 p = 0; p < fn->param_count; ++p) {
                if (p) out_ << ", ";
                out_ << type_name(fn->locals[p].type);
            }
            out_ << ");\n";
            continue;
        }
        out_ << "static " << type_name(fn->result) << " " << function_name(i) << "(";
        if (fn->param_count == 0) out_ << "void";
        for (u32 p = 0; p < fn->param_count; ++p) {
            if (p) out_ << ", ";
            out_ << type_name(fn->locals[p].type);
        }
        out_ << ");\n";
    }
    out_ << "\n";
}

void CBackend::emit_task_trampolines(const MirProgram &program) {
    bool any = false;
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        if (!program.functions[i]->is_async) continue;
        if (!any) {
            out_ << "/* ---- async task trampolines ---- */\n\n";
            any = true;
        }
        const MirFunction &fn = *program.functions[i];

        // The arguments are copied into a context block that the runtime owns,
        // so the spawning frame may return before the worker reads them.
        out_ << "typedef struct {";
        for (u32 p = 0; p < fn.param_count; ++p) {
            out_ << " " << type_name(fn.locals[p].type) << " a" << p << ";";
        }
        if (fn.param_count == 0) out_ << " char pp_empty;";
        out_ << " } ppctx" << i << ";\n";

        out_ << "static uintptr_t pptask" << i << "(void *raw) {\n";
        out_ << "    ppctx" << i << " *c = (ppctx" << i << " *)raw;\n";
        out_ << "    (void)c;\n";

        const bool returns_value = fn.result && fn.result->kind != TypeKind::Void;
        if (returns_value && fn.result->kind == TypeKind::Float) {
            // A double does not fit a uintptr_t by conversion, so its bits are
            // copied instead.
            out_ << "    double value = " << function_name(i) << "(";
        } else if (returns_value) {
            out_ << "    uintptr_t value = (uintptr_t)" << function_name(i) << "(";
        } else {
            out_ << "    " << function_name(i) << "(";
        }
        for (u32 p = 0; p < fn.param_count; ++p) {
            if (p) out_ << ", ";
            out_ << "c->a" << p;
        }
        out_ << ");\n";

        if (returns_value && fn.result->kind == TypeKind::Float) {
            out_ << "    uintptr_t bits = 0;\n";
            out_ << "    memcpy(&bits, &value, sizeof(bits) < sizeof(value) ? sizeof(bits) : sizeof(value));\n";
            out_ << "    return bits;\n";
        } else if (returns_value) {
            out_ << "    return value;\n";
        } else {
            out_ << "    return 0;\n";
        }
        out_ << "}\n";

        out_ << "static pp_task *pptask_spawn" << i << "(";
        if (fn.param_count == 0) out_ << "void";
        for (u32 p = 0; p < fn.param_count; ++p) {
            if (p) out_ << ", ";
            out_ << type_name(fn.locals[p].type) << " a" << p;
        }
        out_ << ") {\n";
        out_ << "    ppctx" << i << " context;\n";
        out_ << "    memset(&context, 0, sizeof(context));\n";
        for (u32 p = 0; p < fn.param_count; ++p) {
            out_ << "    context.a" << p << " = a" << p << ";\n";
        }
        out_ << "    return pp_task_spawn(pptask" << i
             << ", &context, (int64_t)sizeof(context));\n";
        out_ << "}\n\n";
    }
}

void CBackend::emit_entry(const MirProgram &program) {
    out_ << "/* ---- program entry ---- */\n\n";
    out_ << "int main(int argc, char **argv) {\n";
    out_ << "    pp_runtime_init(argc, argv);\n";
    if (program.entry < program.functions.size()) {
        out_ << "    " << function_name(program.entry) << "();\n";
    }
    // Cleanup is also registered with atexit; calling it here makes the normal
    // path deterministic and keeps it idempotent for the atexit run.
    out_ << "    pp_runtime_cleanup();\n";
    out_ << "    return 0;\n";
    out_ << "}\n";
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

bool CBackend::emit(const MirProgram &program, const CodegenOptions &options, std::string &out) {
    program_ = &program;
    options_ = options;
    out_.str({});
    out_.clear();
    aggregates_.clear();
    aggregate_names_.clear();
    emit_state_.clear();

    out_ << "/* Generated by ppc, the PunPun compiler.\n";
    if (!options.source_name.empty()) out_ << " * Source: " << options.source_name << "\n";
    out_ << " *\n";
    out_ << " * Do not edit. Rebuild from the .pp sources instead.\n";
    out_ << " * Arithmetic is "
         << (options.unchecked_arithmetic ? "unchecked (--unchecked)" : "checked, per the language spec")
         << ".\n";
    out_ << " */\n\n";

    out_ << "#include <stdbool.h>\n";
    out_ << "#include <stdint.h>\n";
    out_ << "#include <string.h>\n";
    out_ << "#include \"ppcrt.h\"\n\n";

    collect_aggregates(program);
    emit_aggregates();

    // A user-supplied foreign block is pasted verbatim ahead of the program so
    // an `extern native fn` can resolve against it in the same translation unit.
    if (!program.injections.empty()) {
        out_ << "/* ---- foreign injection ---- */\n\n";
        for (const InjectionDecl &injection : program.injections) {
            const std::string language = types_.interner().text(injection.language);
            if (language != "c") {
                diagnostics_
                    .warning(Code::BackendUnavailable,
                             "the C backend cannot inline a '" + language + "' block")
                    .label(injection.span)
                    .with_help("compile it separately and link the object, or use @inject->c");
                continue;
            }
            out_ << types_.interner().text(injection.source) << "\n";
        }
        out_ << "\n";
    }

    emit_prototypes(program);
    emit_task_trampolines(program);

    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        if (program.functions[i]->is_extern_native) continue;
        emit_function(*program.functions[i], i);
    }

    emit_entry(program);

    out = out_.str();
    return !diagnostics_.has_errors();
}

}  // namespace ppc
