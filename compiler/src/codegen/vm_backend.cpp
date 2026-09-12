#include "ppc/codegen/vm_backend.hpp"

#include <cstring>
#include <sstream>

#include "ppc/sema/builtins.hpp"

namespace ppc {

// ---------------------------------------------------------------------------
// Frame layout
// ---------------------------------------------------------------------------

u32 VmBackend::slot_for_register(Reg id) const {
    // A frame is [locals..., registers...]. Keeping locals first means a call's
    // parameters land at slots 0..n-1, so the caller can copy arguments into
    // place without a separate mapping table.
    return local_count_ + id;
}

u32 VmBackend::intern_string(const std::string &text) {
    auto it = string_index_.find(text);
    if (it != string_index_.end()) return it->second;
    const u32 index = static_cast<u32>(program_->strings.size());
    program_->strings.push_back(text);
    string_index_.emplace(text, index);
    return index;
}

/// Appends an instruction and returns a reference to it.
///
/// The reference points into a vector, so a later emit invalidates it. Every
/// caller therefore finishes writing to the returned instruction before it
/// emits anything else — copies and other helper instructions are emitted
/// first, into locals, and only then is the main instruction created.
Instr &VmBackend::emit_instruction(Op op, const MirInst &source) {
    program_->code.emplace_back();
    Instr &instruction = program_->code.back();
    instruction.op = op;
    instruction.line = source.span.start;
    return instruction;
}


// ---------------------------------------------------------------------------
// Value-struct copying
//
// The VM boxes every aggregate into a heap block, so two slots holding the same
// struct would alias. That is right for an `object`, which has identity, and
// harmless for an enum, which cannot be mutated in place. It is wrong for a
// `struct`, which the language specifies as a value: assigning one must produce
// an independent copy, all the way down through nested value fields.
// ---------------------------------------------------------------------------

u32 VmBackend::copy_decl_for(const Type *type) const {
    if (!type || type->kind != TypeKind::Struct) return 0xFFFFFFFFu;
    if (type->decl >= program_->layouts.size()) return 0xFFFFFFFFu;
    if (!program_->layouts[type->decl].copy_by_value) return 0xFFFFFFFFu;
    return type->decl;
}

u32 VmBackend::scratch_slot() { return frame_size_++; }

u32 VmBackend::copy_if_value_struct(const Type *type, u32 slot, const MirInst &at) {
    const u32 decl = copy_decl_for(type);
    if (decl == 0xFFFFFFFFu) return slot;
    Instr &out = emit_instruction(Op::CopyStruct, at);
    out.dest = scratch_slot();
    out.a = slot;
    out.imm = decl;
    return out.dest;
}

void VmBackend::build_layouts() {
    program_->layouts.assign(types_.struct_count(), StructLayout{});
    for (std::size_t i = 0; i < types_.struct_count(); ++i) {
        const StructInfo &info = types_.struct_at(static_cast<u32>(i));
        StructLayout &layout = program_->layouts[i];
        layout.slot_count = static_cast<u32>(info.fields.size());
        // Objects are handles, so copying one would break the identity the
        // language guarantees.
        layout.copy_by_value = !info.is_reference;
        for (std::size_t f = 0; f < info.fields.size(); ++f) {
            const Type *field = info.fields[f].type;
            if (field && field->kind == TypeKind::Struct &&
                field->decl < types_.struct_count() &&
                !types_.struct_at(field->decl).is_reference) {
                layout.nested.emplace_back(static_cast<u32>(f), field->decl);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Operator selection
// ---------------------------------------------------------------------------

Op VmBackend::binary_opcode(BinaryOp op, TypeKind operand, bool unchecked) const {
    const bool is_float = (operand == TypeKind::Float);
    const bool is_str = (operand == TypeKind::Str);

    switch (op) {
        case BinaryOp::Add:
            if (is_str) return Op::ConcatStr;
            if (is_float) return Op::AddFloat;
            return unchecked ? Op::UncheckedAddInt : Op::AddInt;
        case BinaryOp::Subtract:
            if (is_float) return Op::SubFloat;
            return unchecked ? Op::UncheckedSubInt : Op::SubInt;
        case BinaryOp::Multiply:
            if (is_float) return Op::MulFloat;
            return unchecked ? Op::UncheckedMulInt : Op::MulInt;
        case BinaryOp::Divide:
            // Integer division always goes through the checked opcode: dividing
            // by zero has no defined machine result to fall back on.
            return is_float ? Op::DivFloat : Op::DivInt;
        case BinaryOp::Modulo: return Op::ModInt;

        case BinaryOp::Equal:
            return is_str ? Op::EqStr : (is_float ? Op::EqFloat : Op::EqInt);
        case BinaryOp::NotEqual:
            return is_str ? Op::NeStr : (is_float ? Op::NeFloat : Op::NeInt);
        case BinaryOp::Less:
            return is_str ? Op::LtStr : (is_float ? Op::LtFloat : Op::LtInt);
        case BinaryOp::LessEqual:
            return is_str ? Op::LeStr : (is_float ? Op::LeFloat : Op::LeInt);
        case BinaryOp::Greater:
            return is_str ? Op::GtStr : (is_float ? Op::GtFloat : Op::GtInt);
        case BinaryOp::GreaterEqual:
            return is_str ? Op::GeStr : (is_float ? Op::GeFloat : Op::GeInt);

        case BinaryOp::BitAnd: return Op::BitAnd;
        case BinaryOp::BitOr: return Op::BitOr;
        case BinaryOp::BitXor: return Op::BitXor;
        case BinaryOp::ShiftLeft: return Op::ShiftLeft;
        case BinaryOp::ShiftRight: return Op::ShiftRight;

        // The MIR builder turns these into branches, so they never reach here.
        case BinaryOp::And: return Op::BitAnd;
        case BinaryOp::Or: return Op::BitOr;
    }
    return Op::Halt;
}

// ---------------------------------------------------------------------------
// Instruction lowering
// ---------------------------------------------------------------------------

void VmBackend::compile_instruction(const MirFunction &fn, const MirInst &source) {
    auto reg = [&](Reg id) { return slot_for_register(id); };

    switch (source.op) {
        case MirOp::ConstInt: {
            Instr &out = emit_instruction(Op::ConstInt, source);
            out.dest = reg(source.dest);
            out.imm = source.imm;
            return;
        }
        case MirOp::ConstBool: {
            Instr &out = emit_instruction(Op::ConstBool, source);
            out.dest = reg(source.dest);
            out.imm = source.imm ? 1 : 0;
            return;
        }
        case MirOp::ConstFloat: {
            Instr &out = emit_instruction(Op::ConstFloat, source);
            out.dest = reg(source.dest);
            out.fimm = source.fimm;
            return;
        }
        case MirOp::ConstStr: {
            Instr &out = emit_instruction(Op::ConstStr, source);
            out.dest = reg(source.dest);
            out.imm = intern_string(types_.interner().text(source.text));
            return;
        }
        case MirOp::LoadLocal: {
            Instr &out = emit_instruction(Op::LoadLocal, source);
            out.dest = reg(source.dest);
            out.a = slot_for_local(source.index);
            return;
        }
        case MirOp::StoreLocal: {
            // Binding a value struct to a local gives it a new home, so it gets
            // its own copy.
            const Type *type = source.a < fn.reg_types.size() ? fn.reg_types[source.a] : nullptr;
            const u32 value = copy_if_value_struct(type, reg(source.a), source);
            Instr &out = emit_instruction(Op::StoreLocal, source);
            out.dest = slot_for_local(source.index);
            out.a = value;
            return;
        }
        case MirOp::LocalAddr: {
            Instr &out = emit_instruction(Op::LocalAddr, source);
            out.dest = reg(source.dest);
            out.a = slot_for_local(source.index);
            return;
        }
        case MirOp::Binary: {
            const TypeKind operand = static_cast<TypeKind>(source.imm);
            Instr &out = emit_instruction(
                binary_opcode(source.binary_op, operand, options_.unchecked_arithmetic), source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            out.b = reg(source.b);
            return;
        }
        case MirOp::MakeClosure: {
            // Captures become independent homes just like parameters/fields.
            // Value structs therefore need a deep copy before their handle is
            // written into the closure environment.
            std::vector<u32> captures;
            captures.reserve(source.args.size());
            for (Reg capture : source.args) {
                const Type *type = capture < fn.reg_types.size() ? fn.reg_types[capture] : nullptr;
                captures.push_back(copy_if_value_struct(type, reg(capture), source));
            }
            Instr &out = emit_instruction(Op::MakeClosure, source);
            out.dest = reg(source.dest);
            out.imm = source.target;
            out.arg_offset = static_cast<u32>(program_->arguments.size());
            out.arg_count = static_cast<u32>(captures.size());
            for (u32 slot : captures) program_->arguments.push_back(slot);
            return;
        }
        case MirOp::LoadCapture: {
            Instr &out = emit_instruction(Op::LoadCapture, source);
            out.dest = reg(source.dest);
            out.imm = source.index;
            return;
        }
        case MirOp::StoreCapture: {
            Instr &out = emit_instruction(Op::StoreCapture, source);
            out.a = reg(source.a);
            out.imm = source.index;
            return;
        }
        case MirOp::Unary: {
            const Type *operand = source.a < fn.reg_types.size() ? fn.reg_types[source.a] : nullptr;
            Op op = Op::NegInt;
            switch (source.unary_op) {
                case UnaryOp::Negate:
                    op = (operand && operand->kind == TypeKind::Float) ? Op::NegFloat : Op::NegInt;
                    break;
                case UnaryOp::Not: op = Op::NotBool; break;
                case UnaryOp::BitNot: op = Op::BitNot; break;
                default: op = Op::Move; break;
            }
            Instr &out = emit_instruction(op, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            return;
        }
        case MirOp::Call:
        case MirOp::CallIndirect:
        case MirOp::CallBuiltin: {
            const bool builtin = (source.op == MirOp::CallBuiltin);
            const bool indirect = (source.op == MirOp::CallIndirect);
            const MirFunction *target =
                (!builtin && source.target < mir_->functions.size())
                    ? mir_->functions[source.target]
                    : nullptr;

            // Calling an async function spawns it. The VM runs tasks inline, so
            // Spawn evaluates the body immediately and boxes the result.
            Op op = Op::Call;
            if (builtin) op = Op::CallBuiltin;
            else if (indirect) op = Op::CallIndirect;
            else if (target && target->is_async) op = Op::Spawn;

            // Argument copies emit instructions of their own, which can
            // reallocate the code vector, so they run before the call
            // instruction is created and its fields are filled in.
            std::vector<u32> argument_slots;
            argument_slots.reserve(source.args.size());
            for (Reg argument : source.args) {
                const Type *type =
                    argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
                argument_slots.push_back(copy_if_value_struct(type, reg(argument), source));
            }

            Instr &out = emit_instruction(op, source);
            out.dest = source.dest == kNoReg ? 0xFFFFFFFFu : reg(source.dest);
            out.imm = source.target;
            if (indirect) out.a = reg(source.a);

            // print/println/say are polymorphic in the source language but the
            // runtime has one entry per type. The choice depends on the
            // argument's static type, which only exists here, so it is baked
            // into the instruction rather than rediscovered at runtime.
            if (builtin && source.target < builtin_table().size()) {
                const std::string builtin_name = builtin_table()[source.target].name;
                if (builtin_name == "print" || builtin_name == "println" ||
                    builtin_name == "say") {
                    const Type *argument = source.args.empty() ? nullptr
                                           : (source.args[0] < fn.reg_types.size()
                                                  ? fn.reg_types[source.args[0]]
                                                  : nullptr);
                    u32 formatter = kFormatStr;
                    if (argument) {
                        switch (argument->kind) {
                            case TypeKind::Int: formatter = kFormatInt; break;
                            case TypeKind::Float: formatter = kFormatFloat; break;
                            case TypeKind::Bool: formatter = kFormatBool; break;
                            case TypeKind::Str: formatter = kFormatStr; break;
                            default: formatter = kFormatInt; break;
                        }
                    }
                    out.b = formatter;
                }
            }

            out.arg_offset = static_cast<u32>(program_->arguments.size());
            out.arg_count = static_cast<u32>(argument_slots.size());
            for (u32 slot : argument_slots) program_->arguments.push_back(slot);

            // Reading an aggregate out of a list must yield an independent
            // value, or mutating what was read would reach back into the list.
            // Arguments were already copied by the loop above.
            if (builtin && source.dest != kNoReg && source.target < builtin_table().size() &&
                (builtin_table()[source.target].result == BuiltinType::ListElement ||
                 builtin_table()[source.target].result == BuiltinType::MapValue)) {
                const u32 decl = copy_decl_for(source.type);
                if (decl != 0xFFFFFFFFu) {
                    Instr &copy = emit_instruction(Op::CopyStruct, source);
                    copy.dest = reg(source.dest);
                    copy.a = reg(source.dest);
                    copy.imm = decl;
                }
            }
            return;
        }
        case MirOp::MakeStruct: {
            // Each initializer becomes a field, so value structs are copied in.
            std::vector<u32> field_slots;
            field_slots.reserve(source.args.size());
            for (Reg argument : source.args) {
                const Type *type =
                    argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
                field_slots.push_back(copy_if_value_struct(type, reg(argument), source));
            }

            Instr &out = emit_instruction(Op::MakeStruct, source);
            out.dest = reg(source.dest);
            // Block size is the declared field count, not the argument count:
            // a constructor may leave trailing fields at their zero value.
            u32 size = static_cast<u32>(source.args.size());
            if (source.type && source.type->is_aggregate() &&
                source.type->kind != TypeKind::Enum &&
                source.type->decl < types_.struct_count()) {
                size = static_cast<u32>(types_.struct_at(source.type->decl).fields.size());
            }
            out.imm = size;
            out.arg_offset = static_cast<u32>(program_->arguments.size());
            out.arg_count = static_cast<u32>(field_slots.size());
            for (u32 slot : field_slots) program_->arguments.push_back(slot);
            return;
        }
        case MirOp::GetField: {
            Instr &out = emit_instruction(Op::GetField, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            out.imm = source.index;
            // Reading a nested value struct yields a copy, exactly as it would
            // in C. Without this, `w.inner.x = 1` would write straight through
            // to the field instead of to a temporary that is then stored back.
            const u32 decl = copy_decl_for(source.type);
            if (decl != 0xFFFFFFFFu) {
                const u32 raw = out.dest;
                Instr &copy = emit_instruction(Op::CopyStruct, source);
                copy.dest = raw;
                copy.a = raw;
                copy.imm = decl;
            }
            return;
        }
        case MirOp::SetField: {
            const Type *type = source.b < fn.reg_types.size() ? fn.reg_types[source.b] : nullptr;
            const u32 value = copy_if_value_struct(type, reg(source.b), source);
            Instr &out = emit_instruction(Op::SetField, source);
            out.a = reg(source.a);
            out.b = value;
            out.imm = source.index;
            return;
        }
        case MirOp::MakeEnum: {
            // A payload slot is a new home for the value, so a value struct
            // stored into a variant is copied like any other field.
            std::vector<u32> payload_slots;
            payload_slots.reserve(source.args.size());
            for (Reg argument : source.args) {
                const Type *type =
                    argument < fn.reg_types.size() ? fn.reg_types[argument] : nullptr;
                payload_slots.push_back(copy_if_value_struct(type, reg(argument), source));
            }

            Instr &out = emit_instruction(Op::MakeEnum, source);
            out.dest = reg(source.dest);
            out.imm = source.index;  // variant tag
            out.arg_offset = static_cast<u32>(program_->arguments.size());
            out.arg_count = static_cast<u32>(payload_slots.size());
            for (u32 slot : payload_slots) program_->arguments.push_back(slot);
            return;
        }
        case MirOp::EnumTag: {
            Instr &out = emit_instruction(Op::EnumTag, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            return;
        }
        case MirOp::EnumPayload: {
            Instr &out = emit_instruction(Op::EnumPayload, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            out.imm = source.slot;
            return;
        }
        case MirOp::MakeList: {
            Instr &out = emit_instruction(Op::MakeList, source);
            out.dest = reg(source.dest);
            out.arg_offset = static_cast<u32>(program_->arguments.size());
            out.arg_count = static_cast<u32>(source.args.size());
            for (Reg argument : source.args) program_->arguments.push_back(reg(argument));
            return;
        }
        case MirOp::GetIndex: {
            const Type *base = source.a < fn.reg_types.size() ? fn.reg_types[source.a] : nullptr;
            const bool is_slice = base && base->kind == TypeKind::Slice;
            Instr &out = emit_instruction(is_slice ? Op::GetSliceIndex : Op::GetIndex, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            out.b = reg(source.b);
            return;
        }
        case MirOp::SetIndex: {
            Instr &out = emit_instruction(Op::SetIndex, source);
            out.a = reg(source.a);
            out.b = reg(source.b);
            out.c = reg(source.c);
            return;
        }
        case MirOp::Deref: {
            Instr &out = emit_instruction(Op::Deref, source);
            out.dest = reg(source.dest);
            out.a = reg(source.a);
            return;
        }
        case MirOp::StoreDeref: {
            Instr &out = emit_instruction(Op::StoreDeref, source);
            out.a = reg(source.a);
            out.b = reg(source.b);
            return;
        }
        case MirOp::Await: {
            Instr &out = emit_instruction(Op::Await, source);
            out.dest = source.dest == kNoReg ? 0xFFFFFFFFu : reg(source.dest);
            out.a = reg(source.a);
            return;
        }
        case MirOp::Drop:
            // The runtime releases everything at cleanup, so a drop is a marker
            // with no bytecode.
            return;

        case MirOp::Jump: {
            Instr &out = emit_instruction(Op::Jump, source);
            pending_jumps_.emplace_back(static_cast<u32>(program_->code.size() - 1),
                                        source.then_block);
            (void)out;
            return;
        }
        case MirOp::Branch: {
            // Emitted as "branch if true to X" followed by "jump to Y", which
            // keeps the instruction fixed-width without a second target field.
            Instr &branch = emit_instruction(Op::BranchTrue, source);
            branch.a = reg(source.a);
            pending_jumps_.emplace_back(static_cast<u32>(program_->code.size() - 1),
                                        source.then_block);
            emit_instruction(Op::Jump, source);
            pending_jumps_.emplace_back(static_cast<u32>(program_->code.size() - 1),
                                        source.else_block);
            return;
        }
        case MirOp::Return: {
            if (source.a == kNoReg) {
                emit_instruction(Op::ReturnVoid, source);
            } else {
                Instr &out = emit_instruction(Op::Return, source);
                out.a = reg(source.a);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Function and program lowering
// ---------------------------------------------------------------------------

void VmBackend::compile_function(const MirFunction &fn, std::size_t index) {
    BytecodeFunction &out = program_->functions[index];
    out.name = fn.name;
    out.param_count = fn.param_count;
    out.local_count = static_cast<u32>(fn.locals.size());
    out.register_count = static_cast<u32>(fn.reg_types.size());
    out.frame_size = out.local_count + out.register_count;
    out.returns_value = fn.result && fn.result->kind != TypeKind::Void;
    out.is_async = fn.is_async;
    out.is_extern_native = fn.is_extern_native;
    out.native_symbol = fn.native_symbol;

    if (fn.is_extern_native) {
        out.entry = 0;
        out.instruction_count = 0;
        return;
    }

    function_ = &fn;
    local_count_ = out.local_count;
    // Copy temporaries are allocated past the register range, so the frame can
    // still grow while the body is being compiled.
    frame_size_ = out.local_count + out.register_count;
    block_starts_.assign(fn.blocks.size(), 0xFFFFFFFFu);
    pending_jumps_.clear();

    // A value-struct local needs a block of its own from the start, matching
    // the `= {0}` the C backend emits for an aggregate local.
    for (std::size_t i = 0; i < fn.locals.size(); ++i) {
        const u32 decl = copy_decl_for(fn.locals[i].type);
        if (decl == 0xFFFFFFFFu) continue;
        out.struct_locals.emplace_back(static_cast<u32>(i), decl);
    }

    out.entry = static_cast<u32>(program_->code.size());

    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        block_starts_[block.id] = static_cast<u32>(program_->code.size());
        for (const MirInst &instruction : block.instructions) {
            compile_instruction(fn, instruction);
        }
    }

    // Every jump target is known now, so the placeholders can be filled in.
    for (const auto &pending : pending_jumps_) {
        const u32 site = pending.first;
        const BlockId target = pending.second;
        if (target >= block_starts_.size() || block_starts_[target] == 0xFFFFFFFFu) {
            // A jump to an unreachable block cannot execute; send it to the
            // function's trailing Halt so a bug surfaces loudly rather than
            // running off the end of the code array.
            program_->code[site].imm = static_cast<i64>(program_->code.size());
            continue;
        }
        program_->code[site].imm = static_cast<i64>(block_starts_[target]);
    }

    // A trailing guard: reaching it means control fell off the end, which the
    // checker should have prevented.
    MirInst sentinel;
    sentinel.span = fn.span;
    emit_instruction(Op::Halt, sentinel);

    out.instruction_count = static_cast<u32>(program_->code.size()) - out.entry;
    out.frame_size = frame_size_;
    function_ = nullptr;
}

bool VmBackend::compile(const MirProgram &program, const CodegenOptions &options,
                        BytecodeProgram &out) {
    program_ = &out;
    mir_ = &program;
    options_ = options;
    string_index_.clear();

    out.code.clear();
    out.functions.assign(program.functions.size(), BytecodeFunction{});
    out.strings.clear();
    out.arguments.clear();
    out.entry = program.entry;
    out.source_name = options.source_name;

    // Layouts must exist before any function is compiled, because deciding
    // whether an assignment needs a copy consults them.
    build_layouts();

    if (!program.injections.empty()) {
        diagnostics_
            .warning(Code::BackendUnavailable,
                     "the bytecode backend cannot compile @inject blocks")
            .label(program.injections.front().span)
            .with_help("use --backend=c when a program injects foreign source");
    }

    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const MirFunction *fn = program.functions[i];
        if (fn->is_extern_native) {
            diagnostics_
                .error(Code::BackendUnavailable,
                       "the bytecode backend cannot call native function '" +
                           fn->native_symbol + "'")
                .with_help("use --backend=c to link against native code");
            continue;
        }
        compile_function(*fn, i);
    }

    return !diagnostics_.has_errors();
}

bool VmBackend::emit(const MirProgram &program, const CodegenOptions &options, std::string &out) {
    BytecodeProgram bytecode;
    if (!compile(program, options, bytecode)) return false;
    out = bytecode.serialize();
    return true;
}

}  // namespace ppc
