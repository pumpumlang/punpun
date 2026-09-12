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
    const MirFunction *target = (in.op != MirOp::CallIndirect &&
                                 in.target < program_->functions.size())
                                    ? program_->functions[in.target]
                                    : nullptr;
    ParameterUsage parameter_usage;
    const bool can_elide_parameter_copies =
        target && !target->is_async && !target->is_extern_native && in.op != MirOp::CallIndirect;
    if (can_elide_parameter_copies) parameter_usage = analyze_parameters(*target);

    // Aggregate copies emit their own calls, so they all happen before any
    // argument register is loaded.  A direct synchronous callee that provably
    // only reads a value-struct parameter can borrow the caller's block for the
    // duration of the call; value semantics are unchanged because it neither
    // mutates nor lets that block escape.
    std::vector<int> slots;
    std::vector<const Type *> kinds;
    slots.reserve(in.args.size());
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        const Reg argument = in.args[i];
        const Type *type = argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
        const bool read_only_struct =
            can_elide_parameter_copies && copy_decl_for(type) != 0xFFFFFFFFu &&
            i < parameter_usage.read_only.size() && parameter_usage.read_only[i];
        if (read_only_struct) {
            slots.push_back(register_offset(argument));
            ++elided_copies_;
        } else {
            slots.push_back(copy_if_value_struct(type, register_offset(argument), argument));
        }
        kinds.push_back(type);
    }

    // Resolve an indirect closure target before loading source arguments. The
    // runtime null-check is allowed to clobber ABI argument registers here; we
    // fill them only afterwards. %r10 carries the closure into generated code.
    if (in.op == MirOp::CallIndirect) {
        load_value("%rdi", in.a);
        out_ << "\tmovb\t$0, %al\n";
        out_ << "\tcall\tpp_closure_target\n";
        out_ << "\tmovq\t%rax, %r11\n";
        load_value("%r10", in.a);
        out_ << "\tleaq\tpp_fn_table(%rip), %rax\n";
        out_ << "\tmovq\t(%rax,%r11,8), %r11\n";
    }

    // System V classification: integers and pointers use the six GP argument
    // registers, doubles use the eight SSE argument registers, and any scalar
    // that exhausts its register class is passed in an eightbyte stack slot.
    int gp = 0;
    int sse = 0;
    std::vector<bool> on_stack(slots.size(), false);
    std::size_t stack_count = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (kinds[i] && kinds[i]->kind == TypeKind::Float) {
            if (sse < 8) ++sse;
            else { on_stack[i] = true; ++stack_count; }
        } else {
            if (gp < 6) ++gp;
            else { on_stack[i] = true; ++stack_count; }
        }
    }

    // %rsp is 16-byte aligned between calls.  Stack arguments are pushed in
    // reverse source order so the first spilled argument is at 8(%rsp) after
    // the call pushes its return address.  An odd slot count needs one padding
    // eightbyte below the arguments to preserve call-site alignment.
    const std::size_t stack_pad = (stack_count & 1u) ? 8u : 0u;
    if (stack_pad) out_ << "\tsubq\t$8, %rsp\n";
    for (std::size_t i = slots.size(); i-- > 0;) {
        if (!on_stack[i]) continue;
        load("%rax", slots[i]);
        out_ << "\tpushq\t%rax\n";
    }

    gp = 0;
    sse = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (on_stack[i]) continue;
        if (kinds[i] && kinds[i]->kind == TypeKind::Float) {
            out_ << "\tmovsd\t" << slots[i] << "(%rbp), %xmm" << sse << "\n";
            ++sse;
        } else {
            load(kIntArgs[gp], slots[i]);
            ++gp;
        }
    }

    // %al holds the number of SSE registers used, which variadic callees read.
    // Setting it unconditionally is harmless for non-variadic ones.
    out_ << "\tmovb\t$" << sse << ", %al\n";

    if (in.op == MirOp::CallIndirect) {
        out_ << "\tcall\t*%r11\n";
    } else {
        // A direct call has no closure environment. The callee saves this
        // hidden register in its frame before doing any work.
        out_ << "\txorl\t%r10d, %r10d\n";
        if (target && target->is_async) {
            out_ << "\tcall\tpptask_spawn" << in.target << "\n";
        } else {
            out_ << "\tcall\t" << function_label(in.target) << "\n";
        }
    }
    const std::size_t stack_bytes = stack_count * 8u + stack_pad;
    if (stack_bytes) out_ << "\taddq\t$" << stack_bytes << ", %rsp\n";

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
            store_value("%rax", in.dest);
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
            store_value("%rax", in.dest);
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

    // Runtime builtins use the same SysV argument classification as ordinary
    // calls. Before 1.5 this path assumed every builtin fit the register set,
    // which was accidentally true until richer APIs such as GUI drawing grew
    // past six integer/pointer arguments.
    int gp = 0;
    int sse = 0;
    std::vector<bool> on_stack(in.args.size(), false);
    std::size_t stack_count = 0;
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        const Type *type = arg_type(i);
        if (type && type->kind == TypeKind::Float && !is_element(i)) {
            if (sse < 8) ++sse;
            else { on_stack[i] = true; ++stack_count; }
        } else {
            if (gp < 6) ++gp;
            else { on_stack[i] = true; ++stack_count; }
        }
    }

    const std::size_t stack_pad = (stack_count & 1u) ? 8u : 0u;
    if (stack_pad) out_ << "\tsubq\t$8, %rsp\n";
    for (std::size_t i = in.args.size(); i-- > 0;) {
        if (!on_stack[i]) continue;
        load("%rax", slots[i]);
        out_ << "\tpushq\t%rax\n";
    }

    gp = 0;
    sse = 0;
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        if (on_stack[i]) continue;
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
    const std::size_t stack_bytes = stack_count * 8u + stack_pad;
    if (stack_bytes) out_ << "\taddq\t$" << stack_bytes << ", %rsp\n";

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
            store_value("%rax", in.dest);
        }
        // Reading an aggregate out of a list yields a copy, so mutating what was
        // read cannot reach back into the list.
        if (element_result && is_boxed(in.type)) {
            const int copied = copy_if_value_struct(in.type, register_offset(in.dest));
            load("%rax", copied);
            store_value("%rax", in.dest);
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
            store_value("%rax", in.dest);
            return;
        case MirOp::ConstBool:
            out_ << "\tmovq\t$" << (in.imm ? 1 : 0) << ", %rax\n";
            store_value("%rax", in.dest);
            return;
        case MirOp::ConstFloat: {
            // A double is emitted as its bit pattern and moved through a GP
            // register, which avoids needing a constant pool entry.
            u64 bits = 0;
            std::memcpy(&bits, &in.fimm, sizeof(bits));
            out_ << "\tmovabsq\t$" << static_cast<long long>(bits) << ", %rax\n";
            store_value("%rax", in.dest);
            return;
        }
        case MirOp::ConstStr: {
            const u32 index = intern_string(types_.interner().text(in.text));
            out_ << "\tleaq\t.Lstr" << index << "(%rip), %rax\n";
            store_value("%rax", in.dest);
            return;
        }

        case MirOp::LoadLocal:
            load("%rax", local_offset(in.index));
            store_value("%rax", in.dest);
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
            store_value("%rax", in.dest);
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
                        load_value("%rax", in.a);
                        if (options_.unchecked_arithmetic) {
                            out_ << "\tnegq\t%rax\n";
                        } else {
                            checked_op("negq\t%rax", ".Ltrap_neg");
                        }
                        store_value("%rax", in.dest);
                    }
                    return;
                case UnaryOp::Not:
                    load_value("%rax", in.a);
                    out_ << "\ttestq\t%rax, %rax\n\tsete\t%al\n\tmovzbq\t%al, %rax\n";
                    store_value("%rax", in.dest);
                    return;
                case UnaryOp::BitNot:
                    load_value("%rax", in.a);
                    out_ << "\tnotq\t%rax\n";
                    store_value("%rax", in.dest);
                    return;
                default:
                    load_value("%rax", in.a);
                    store_value("%rax", in.dest);
                    return;
            }
        }

        case MirOp::MakeClosure: {
            if (in.args.empty()) {
                const unsigned long long handle =
                    (static_cast<unsigned long long>(in.target) << 1) | 1ULL;
                out_ << "\tmovabsq\t$" << handle << ", %rax\n";
                store_value("%rax", in.dest);
                return;
            }
            // A captured value struct gets an independent heap block. Identity
            // objects and scalar handles stay shared exactly as ordinary
            // assignment/parameter passing specifies.
            std::vector<int> captures;
            captures.reserve(in.args.size());
            for (Reg capture : in.args) {
                const Type *type = capture < fn.reg_types.size() ? fn.reg_types[capture] : nullptr;
                captures.push_back(copy_if_value_struct(type, register_offset(capture), capture));
            }

            out_ << "\tmovq\t$" << in.target << ", %rdi\n";
            out_ << "\tmovq\t$" << in.args.size() << ", %rsi\n";
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\tpp_closure_new\n";
            store_value("%rax", in.dest);
            for (std::size_t i = 0; i < captures.size(); ++i) {
                load_value("%rdi", in.dest);
                out_ << "\tmovq\t$" << i << ", %rsi\n";
                load("%rdx", captures[i]);
                out_ << "\tmovb\t$0, %al\n";
                out_ << "\tcall\tpp_closure_set\n";
            }
            return;
        }
        case MirOp::LoadCapture:
            load("%rdi", closure_offset());
            out_ << "\tmovq\t$" << in.index << ", %rsi\n";
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\tpp_closure_get\n";
            store_value("%rax", in.dest);
            return;
        case MirOp::StoreCapture:
            load("%rdi", closure_offset());
            out_ << "\tmovq\t$" << in.index << ", %rsi\n";
            load_value("%rdx", in.a);
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\tpp_closure_set\n";
            return;

        case MirOp::Call:
        case MirOp::CallIndirect: emit_call(fn, in); return;
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
                store_value("%rax", in.dest);
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
            store_value("%rax", in.dest);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                load("%rcx", fields[i]);
                load("%rax", register_offset(in.dest));
                out_ << "\tmovq\t%rcx, " << (i * 8) << "(%rax)\n";
            }
            return;
        }
        case MirOp::GetField: {
            load_value("%rax", in.a);
            out_ << "\tmovq\t" << (in.index * 8) << "(%rax), %rax\n";
            store_value("%rax", in.dest);
            // Reading a nested value struct yields a copy, matching C.
            const u32 decl = copy_decl_for(in.type);
            if (decl != 0xFFFFFFFFu) {
                const int copied = copy_if_value_struct(in.type, register_offset(in.dest));
                load("%rax", copied);
                store_value("%rax", in.dest);
            }
            return;
        }
        case MirOp::SetField: {
            const Type *type = in.b < fn.reg_types.size() ? fn.reg_types[in.b] : nullptr;
            const int value = copy_if_value_struct(type, register_offset(in.b), in.b);
            load_value("%rax", in.a);
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
                store_value("%rax", in.dest);
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
            store_value("%rax", in.dest);
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
            load_value("%rax", in.a);
            out_ << "\tmovq\t0(%rax), %rax\n";
            store_value("%rax", in.dest);
            return;
        case MirOp::EnumPayload:
            load_value("%rax", in.a);
            out_ << "\tmovq\t" << ((1 + in.slot) * 8) << "(%rax), %rax\n";
            store_value("%rax", in.dest);
            return;

        case MirOp::MakeList: {
            out_ << "\tmovb\t$0, %al\n\tcall\tpp_numbers_new\n";
            store_value("%rax", in.dest);
            for (Reg element : in.args) {
                load_value("%rdi", in.dest);
                load_value("%rsi", element);
                out_ << "\tmovb\t$0, %al\n\tcall\tpp_push\n";
            }
            return;
        }
        case MirOp::GetIndex: {
            const Type *base = in.a < fn.reg_types.size() ? fn.reg_types[in.a] : nullptr;
            const bool slice = base && base->kind == TypeKind::Slice;
            load_value("%rdi", in.a);
            load_value("%rsi", in.b);
            out_ << "\tmovb\t$0, %al\n\tcall\t" << (slice ? "pp_slice_at_i64" : "pp_at") << "\n";
            store_value("%rax", in.dest);
            return;
        }
        case MirOp::SetIndex:
            load_value("%rdi", in.a);
            load_value("%rsi", in.b);
            load_value("%rdx", in.c);
            out_ << "\tmovb\t$0, %al\n\tcall\tpp_put\n";
            return;

        case MirOp::Deref:
            load_value("%rax", in.a);
            out_ << "\tmovq\t0(%rax), %rax\n";
            store_value("%rax", in.dest);
            return;
        case MirOp::StoreDeref:
            load_value("%rax", in.a);
            load_value("%rcx", in.b);
            out_ << "\tmovq\t%rcx, 0(%rax)\n";
            return;

        case MirOp::Await: {
            load_value("%rdi", in.a);
            const char *helper = "pp_task_await_void";
            if (in.type && in.type->kind != TypeKind::Void) {
                if (in.type->kind == TypeKind::Float) helper = "pp_task_await_f64";
                else if (in.type->kind == TypeKind::Int || in.type->kind == TypeKind::Bool)
                    helper = "pp_task_await_i64";
                else helper = "pp_task_await_ptr";
            }
            out_ << "\tmovb\t$0, %al\n";
            out_ << "\tcall\t" << helper << "\n";
            if (in.dest != kNoReg) {
                if (in.type && in.type->kind == TypeKind::Float) {
                    store_sse("%xmm0", register_offset(in.dest));
                } else {
                    store_value("%rax", in.dest);
                }
            }
            return;
        }

        case MirOp::Drop:
            out_ << "\t# drop local " << in.index << "\n";
            return;

        case MirOp::Jump:
            out_ << "\tjmp\t" << block_label(in.then_block) << "\n";
            return;
        case MirOp::Branch:
            load_value("%rax", in.a);
            out_ << "\ttestq\t%rax, %rax\n";
            out_ << "\tjne\t" << block_label(in.then_block) << "\n";
            out_ << "\tjmp\t" << block_label(in.else_block) << "\n";
            return;
        case MirOp::Return:
            if (in.a != kNoReg) {
                if (fn.result && fn.result->kind == TypeKind::Float) {
                    load_sse("%xmm0", register_offset(in.a));
                } else {
                    load_value("%rax", in.a);
                }
            } else {
                out_ << "\txorq\t%rax, %rax\n";
            }
            out_ << "\tjmp\t" << epilogue_label() << "\n";
            return;
    }
}

}  // namespace ppc
