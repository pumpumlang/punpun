#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ppc/codegen/native_backend.hpp"
#include "ppc/sema/builtins.hpp"

namespace ppc {

namespace {
const char *const kIntArgs[6] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
}

std::string NativeBackend::block_label(BlockId block) const {
    // Labels are scoped by function index so two functions cannot collide.
    return ".Lf" + std::to_string(function_index_) + "_bb" + std::to_string(block);
}

std::string NativeBackend::epilogue_label() const {
    return ".Lf" + std::to_string(function_index_) + "_end";
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

void NativeBackend::emit_call(const MirFunction &fn, const MirInst &in) {
    // Aggregate copies emit their own calls, so they all happen before any
    // argument register is loaded.
    std::vector<int> slots;
    std::vector<const Type *> kinds;
    slots.reserve(in.args.size());
    for (Reg argument : in.args) {
        const Type *type = argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
        slots.push_back(copy_if_value_struct(type, register_offset(argument), argument));
        kinds.push_back(type);
    }

    // System V classification: integers and pointers go in the GP registers,
    // doubles in the SSE ones, each counted independently.
    int gp = 0;
    int sse = 0;
    bool overflowed = false;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (kinds[i] && kinds[i]->kind == TypeKind::Float) {
            if (sse >= 8) { overflowed = true; break; }
            out_ << "\tmovsd\t" << slots[i] << "(%rbp), %xmm" << sse << "\n";
            ++sse;
        } else {
            if (gp >= 6) { overflowed = true; break; }
            load(kIntArgs[gp], slots[i]);
            ++gp;
        }
    }

    if (overflowed) {
        diagnostics_
            .error(Code::BackendUnavailable,
                   "the native backend supports at most 6 integer and 8 float arguments")
            .label(in.span)
            .note("stack argument passing is not implemented in this backend")
            .with_help("group the arguments into a struct, or use --backend=c");
        return;
    }

    // %al holds the number of SSE registers used, which variadic callees read.
    // Setting it unconditionally is harmless for non-variadic ones.
    out_ << "\tmovb\t$" << sse << ", %al\n";

    const MirFunction *target = in.target < program_->functions.size()
                                    ? program_->functions[in.target]
                                    : nullptr;
    if (target && target->is_async) {
        diagnostics_
            .error(Code::BackendUnavailable,
                   "the native backend does not implement async task spawning")
            .label(in.span)
            .with_help("use --backend=c for programs that use async");
        return;
    }

    out_ << "\tcall\t" << function_label(in.target) << "\n";

    if (in.dest != kNoReg) {
        const Type *result = in.type;
        if (result && result->kind == TypeKind::Float) {
            store_sse("%xmm0", register_offset(in.dest));
        } else {
            // Applied to PPC-generated callees too. They already return a full
            // 0 or 1, so the extension is redundant there, but relying on that
            // would break the moment a function is replaced by a native one.
            if (result && result->kind == TypeKind::Bool) {
                out_ << "\tmovzbq\t%al, %rax\n";
            }
            store("%rax", register_offset(in.dest));
        }
    }
}

void NativeBackend::emit_builtin(const MirFunction &fn, const MirInst &in) {
    const std::vector<BuiltinSpec> &table = builtin_table();
    if (in.target >= table.size()) return;
    const BuiltinSpec &spec = table[in.target];
    const std::string name = spec.name;

    auto arg_offset = [&](std::size_t i) {
        return register_offset(in.args[i]);
    };
    auto arg_type = [&](std::size_t i) -> const Type * {
        return in.args[i] < fn.reg_types.size() ? fn.reg_types[in.args[i]] : nullptr;
    };

    // print/println/say resolve to a runtime entry chosen by the argument's
    // static type, the same way the C backend does it.
    if (name == "print" || name == "println" || name == "say") {
        const bool newline = (name != "print");
        const Type *type = in.args.empty() ? nullptr : arg_type(0);
        const char *suffix = "str";
        bool sse_argument = false;
        if (type) {
            switch (type->kind) {
                case TypeKind::Int: suffix = "int"; break;
                case TypeKind::Float: suffix = "float"; sse_argument = true; break;
                case TypeKind::Bool: suffix = "bool"; break;
                case TypeKind::Str: suffix = "str"; break;
                default: suffix = "int"; break;
            }
        }
        if (!in.args.empty()) {
            if (sse_argument) out_ << "\tmovsd\t" << arg_offset(0) << "(%rbp), %xmm0\n";
            else load("%rdi", arg_offset(0));
        }
        out_ << "\tmovb\t$" << (sse_argument ? 1 : 0) << ", %al\n";
        out_ << "\tcall\tpp_" << (newline ? "println_" : "print_") << suffix << "\n";
        return;
    }

    // `move` copies the value; `drop` is a compile-time marker only.
    if (name == "move") {
        if (in.dest != kNoReg && !in.args.empty()) {
            load("%rax", arg_offset(0));
            store("%rax", register_offset(in.dest));
        }
        return;
    }
    if (name == "drop") return;

    if (!spec.symbol || !*spec.symbol) return;

    // A List stores raw 8-byte slots, so an element argument travels as bits in
    // a general-purpose register even when it is a float: passing it in an SSE
    // register would have the runtime store a converted value instead of the
    // original bits. A value struct is copied first, so the list owns an
    // independent block and later mutation of the source cannot reach it.
    auto is_element = [&](std::size_t i) {
        return i < spec.params.size() &&
               (spec.params[i] == BuiltinType::ListElement ||
                spec.params[i] == BuiltinType::MapValue);
    };
    auto is_boxed = [](const Type *type) {
        return type && (type->kind == TypeKind::Struct || type->kind == TypeKind::Enum);
    };

    std::vector<int> slots(in.args.size());
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        const Type *type = arg_type(i);
        slots[i] = (is_element(i) && is_boxed(type))
                       ? copy_if_value_struct(type, arg_offset(i), in.args[i])
                       : arg_offset(i);
    }

    int gp = 0;
    int sse = 0;
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        const Type *type = arg_type(i);
        if (type && type->kind == TypeKind::Float && !is_element(i)) {
            out_ << "\tmovsd\t" << slots[i] << "(%rbp), %xmm" << sse << "\n";
            ++sse;
        } else {
            load(kIntArgs[gp], slots[i]);
            ++gp;
        }
    }
    out_ << "\tmovb\t$" << sse << ", %al\n";
    out_ << "\tcall\t" << spec.symbol << "\n";

    if (in.dest != kNoReg) {
        const bool element_result = (spec.result == BuiltinType::ListElement ||
                                     spec.result == BuiltinType::MapValue);
    // A C function returning `bool` sets only %al; the System V ABI leaves the
    // upper 56 bits of %rax unspecified. Storing the whole register would carry
    // that garbage into the value, and a later `not` or branch would then test
    // a nonzero word for a false boolean. Zero-extending is the only correct
    // way to widen it.
        if (in.type && in.type->kind == TypeKind::Bool && !element_result) {
            out_ << "\tmovzbq\t%al, %rax\n";
        }
        if (in.type && in.type->kind == TypeKind::Float && !element_result) {
            store_sse("%xmm0", register_offset(in.dest));
        } else {
            // Element results arrive as raw bits in %rax; storing the whole
            // 8 bytes preserves a double exactly.
            store("%rax", register_offset(in.dest));
        }
        // Reading an aggregate out of a list yields a copy, so mutating what was
        // read cannot reach back into the list.
        if (element_result && is_boxed(in.type)) {
            const int copied = copy_if_value_struct(in.type, register_offset(in.dest));
            load("%rax", copied);
            store("%rax", register_offset(in.dest));
        }
    }
}

// ---------------------------------------------------------------------------
// Instructions
// ---------------------------------------------------------------------------

void NativeBackend::emit_instruction(const MirFunction &fn, const MirInst &in) {
    switch (in.op) {
        case MirOp::ConstInt:
            out_ << "\tmovabsq\t$" << in.imm << ", %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        case MirOp::ConstBool:
            out_ << "\tmovq\t$" << (in.imm ? 1 : 0) << ", %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        case MirOp::ConstFloat: {
            // A double is emitted as its bit pattern and moved through a GP
            // register, which avoids needing a constant pool entry.
            u64 bits = 0;
            std::memcpy(&bits, &in.fimm, sizeof(bits));
            out_ << "\tmovabsq\t$" << static_cast<long long>(bits) << ", %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        }
        case MirOp::ConstStr: {
            const u32 index = intern_string(types_.interner().text(in.text));
            out_ << "\tleaq\t.Lstr" << index << "(%rip), %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        }

        case MirOp::LoadLocal:
            load("%rax", local_offset(in.index));
            store("%rax", register_offset(in.dest));
            return;
        case MirOp::StoreLocal: {
            // Binding a value struct gives it a new home, so it is copied.
            const Type *type = in.a < fn.reg_types.size() ? fn.reg_types[in.a] : nullptr;
            const int source = copy_if_value_struct(type, register_offset(in.a), in.a);
            load("%rax", source);
            store("%rax", local_offset(in.index));
            return;
        }
        case MirOp::LocalAddr:
            out_ << "\tleaq\t" << local_offset(in.index) << "(%rbp), %rax\n";
            store("%rax", register_offset(in.dest));
            return;

        case MirOp::Binary: emit_binary(fn, in); return;

        case MirOp::Unary: {
            const int dest = register_offset(in.dest);
            const int source = register_offset(in.a);
            const Type *type = in.a < fn.reg_types.size() ? fn.reg_types[in.a] : nullptr;
            switch (in.unary_op) {
                case UnaryOp::Negate:
                    if (type && type->kind == TypeKind::Float) {
                        // Flipping the sign bit avoids a subtraction and gives
                        // the right answer for zero and NaN.
                        load("%rax", source);
                        out_ << "\tmovabsq\t$-9223372036854775808, %rcx\n";
                        out_ << "\txorq\t%rcx, %rax\n";
                        store("%rax", dest);
                    } else {
                        load("%rax", source);
                        if (options_.unchecked_arithmetic) {
                            out_ << "\tnegq\t%rax\n";
                        } else {
                            checked_op("negq\t%rax", ".Ltrap_neg");
                        }
                        store("%rax", dest);
                    }
                    return;
                case UnaryOp::Not:
                    load("%rax", source);
                    out_ << "\ttestq\t%rax, %rax\n\tsete\t%al\n\tmovzbq\t%al, %rax\n";
                    store("%rax", dest);
                    return;
                case UnaryOp::BitNot:
                    load("%rax", source);
                    out_ << "\tnotq\t%rax\n";
                    store("%rax", dest);
                    return;
                default:
                    load("%rax", source);
                    store("%rax", dest);
                    return;
            }
        }

        case MirOp::Call: emit_call(fn, in); return;
        case MirOp::CallBuiltin: emit_builtin(fn, in); return;

        case MirOp::MakeStruct: {
            // Aggregates are boxed on the heap, so a struct value is a handle
            // and the frame slot holds a pointer.
            std::vector<int> fields;
            for (Reg argument : in.args) {
                const Type *type = argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
                fields.push_back(copy_if_value_struct(type, register_offset(argument), argument));
            }
            std::size_t slot_count = in.args.size();
            if (in.type && in.type->is_aggregate() && in.type->kind != TypeKind::Enum &&
                in.type->decl < types_.struct_count()) {
                slot_count = types_.struct_at(in.type->decl).fields.size();
            }
            if (escapes_.promoted(in.dest)) {
                // Proven not to outlive this frame, so it lives in the frame.
                // This is the single largest win in the native backend: on
                // aggregate-heavy code it removes an allocation and its later
                // collection from every iteration of a loop.
                const int storage = aggregate_offset(escapes_.frame_slot[in.dest]);
                out_ << "\tleaq\t" << storage << "(%rbp), %rax\n";
                store("%rax", register_offset(in.dest));
                for (std::size_t i = 0; i < slot_count; ++i) {
                    if (i < fields.size()) {
                        load("%rcx", fields[i]);
                    } else {
                        // Fields with no initializer must read as zero. The
                        // heap path gets that from pp_object_alloc; frame
                        // storage is reused across loop iterations, so it has
                        // to be cleared explicitly every time.
                        out_ << "\txorq\t%rcx, %rcx\n";
                    }
                    out_ << "\tmovq\t%rcx, " << (storage + static_cast<int>(i) * 8)
                         << "(%rbp)\n";
                }
                return;
            }

            out_ << "\tmovq\t$" << (slot_count ? slot_count * 8 : 8) << ", %rdi\n";
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\tpp_object_alloc\n";
            store("%rax", register_offset(in.dest));
            for (std::size_t i = 0; i < fields.size(); ++i) {
                load("%rcx", fields[i]);
                load("%rax", register_offset(in.dest));
                out_ << "\tmovq\t%rcx, " << (i * 8) << "(%rax)\n";
            }
            return;
        }
        case MirOp::GetField: {
            load("%rax", register_offset(in.a));
            out_ << "\tmovq\t" << (in.index * 8) << "(%rax), %rax\n";
            store("%rax", register_offset(in.dest));
            // Reading a nested value struct yields a copy, matching C.
            const u32 decl = copy_decl_for(in.type);
            if (decl != 0xFFFFFFFFu) {
                const int copied = copy_if_value_struct(in.type, register_offset(in.dest));
                load("%rax", copied);
                store("%rax", register_offset(in.dest));
            }
            return;
        }
        case MirOp::SetField: {
            const Type *type = in.b < fn.reg_types.size() ? fn.reg_types[in.b] : nullptr;
            const int value = copy_if_value_struct(type, register_offset(in.b), in.b);
            load("%rax", register_offset(in.a));
            load("%rcx", value);
            out_ << "\tmovq\t%rcx, " << (in.index * 8) << "(%rax)\n";
            return;
        }

        case MirOp::MakeEnum: {
            std::vector<int> payload;
            for (Reg argument : in.args) {
                const Type *type = argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
                payload.push_back(copy_if_value_struct(type, register_offset(argument), argument));
            }
            // Slot 0 holds the tag; the payload follows.
            if (escapes_.promoted(in.dest)) {
                const int storage = aggregate_offset(escapes_.frame_slot[in.dest]);
                out_ << "\tleaq\t" << storage << "(%rbp), %rax\n";
                store("%rax", register_offset(in.dest));
                out_ << "\tmovq\t$" << in.index << ", %rcx\n";
                out_ << "\tmovq\t%rcx, " << storage << "(%rbp)\n";
                for (std::size_t i = 0; i < payload.size(); ++i) {
                    load("%rcx", payload[i]);
                    out_ << "\tmovq\t%rcx, "
                         << (storage + static_cast<int>(1 + i) * 8) << "(%rbp)\n";
                }
                return;
            }

            out_ << "\tmovq\t$" << ((1 + in.args.size()) * 8) << ", %rdi\n";
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\tpp_object_alloc\n";
            store("%rax", register_offset(in.dest));
            out_ << "\tmovq\t$" << in.index << ", %rcx\n";
            out_ << "\tmovq\t%rcx, 0(%rax)\n";
            for (std::size_t i = 0; i < payload.size(); ++i) {
                load("%rcx", payload[i]);
                load("%rax", register_offset(in.dest));
                out_ << "\tmovq\t%rcx, " << ((1 + i) * 8) << "(%rax)\n";
            }
            return;
        }
        case MirOp::EnumTag:
            load("%rax", register_offset(in.a));
            out_ << "\tmovq\t0(%rax), %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        case MirOp::EnumPayload:
            load("%rax", register_offset(in.a));
            out_ << "\tmovq\t" << ((1 + in.slot) * 8) << "(%rax), %rax\n";
            store("%rax", register_offset(in.dest));
            return;

        case MirOp::MakeList: {
            out_ << "\tmovb\t$0, %al\n\tcall\tpp_numbers_new\n";
            store("%rax", register_offset(in.dest));
            for (Reg element : in.args) {
                load("%rdi", register_offset(in.dest));
                load("%rsi", register_offset(element));
                out_ << "\tmovb\t$0, %al\n\tcall\tpp_push\n";
            }
            return;
        }
        case MirOp::GetIndex: {
            const Type *base = in.a < fn.reg_types.size() ? fn.reg_types[in.a] : nullptr;
            const bool slice = base && base->kind == TypeKind::Slice;
            load("%rdi", register_offset(in.a));
            load("%rsi", register_offset(in.b));
            out_ << "\tmovb\t$0, %al\n\tcall\t" << (slice ? "pp_slice_at_i64" : "pp_at") << "\n";
            store("%rax", register_offset(in.dest));
            return;
        }
        case MirOp::SetIndex:
            load("%rdi", register_offset(in.a));
            load("%rsi", register_offset(in.b));
            load("%rdx", register_offset(in.c));
            out_ << "\tmovb\t$0, %al\n\tcall\tpp_put\n";
            return;

        case MirOp::Deref:
            load("%rax", register_offset(in.a));
            out_ << "\tmovq\t0(%rax), %rax\n";
            store("%rax", register_offset(in.dest));
            return;
        case MirOp::StoreDeref:
            load("%rax", register_offset(in.a));
            load("%rcx", register_offset(in.b));
            out_ << "\tmovq\t%rcx, 0(%rax)\n";
            return;

        case MirOp::Await:
            diagnostics_
                .error(Code::BackendUnavailable,
                       "the native backend does not implement await")
                .label(in.span)
                .with_help("use --backend=c for programs that use async");
            return;

        case MirOp::Drop:
            out_ << "\t# drop local " << in.index << "\n";
            return;

        case MirOp::Jump:
            out_ << "\tjmp\t" << block_label(in.then_block) << "\n";
            return;
        case MirOp::Branch:
            load("%rax", register_offset(in.a));
            out_ << "\ttestq\t%rax, %rax\n";
            out_ << "\tjne\t" << block_label(in.then_block) << "\n";
            out_ << "\tjmp\t" << block_label(in.else_block) << "\n";
            return;
        case MirOp::Return:
            if (in.a != kNoReg) {
                if (fn.result && fn.result->kind == TypeKind::Float) {
                    load_sse("%xmm0", register_offset(in.a));
                } else {
                    load("%rax", register_offset(in.a));
                }
            } else {
                out_ << "\txorq\t%rax, %rax\n";
            }
            out_ << "\tjmp\t" << epilogue_label() << "\n";
            return;
    }
}

}  // namespace ppc
