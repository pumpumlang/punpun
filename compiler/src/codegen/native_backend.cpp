#include "ppc/codegen/native_backend.hpp"

#include <algorithm>
#include <cstdio>

#include "ppc/sema/builtins.hpp"

namespace ppc {

namespace {

/// System V AMD64 integer argument registers, in order.
const char *const kIntegerArguments[6] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

}  // namespace

std::string NativeBackend::sanitize(const std::string &name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        result += ok ? c : '_';
    }
    if (result.empty()) result = "anon";
    return result;
}

std::string NativeBackend::function_label(std::size_t index) const {
    const MirFunction *fn = program_->functions[index];
    if (fn->is_extern_native) return fn->native_symbol;
    return "ppf" + std::to_string(index) + "_" + sanitize(fn->name);
}

u32 NativeBackend::intern_string(const std::string &text) {
    auto it = string_index_.find(text);
    if (it != string_index_.end()) return it->second;
    const u32 index = static_cast<u32>(strings_.size());
    strings_.push_back(text);
    string_index_.emplace(text, index);
    return index;
}

// ---------------------------------------------------------------------------
// Frame layout
//
// Everything lives in memory: locals first, then MIR registers, then scratch
// slots for aggregate copies. All slots are 8 bytes, so the offset arithmetic
// stays trivial and %rbp-relative addressing always works.
// ---------------------------------------------------------------------------

int NativeBackend::local_offset(u32 index) const {
    return -8 * static_cast<int>(index + 1);
}

int NativeBackend::register_offset(Reg id) const {
    const int base = static_cast<int>(function_->locals.size());
    return -8 * (base + static_cast<int>(id) + 1);
}

int NativeBackend::aggregate_offset(u32 slot) const {
    // Frame layout: locals, then registers, then promoted aggregates, then
    // copy scratch. Aggregate storage sits before scratch because its size is
    // known from the escape analysis before the body is emitted, whereas the
    // scratch high-water mark is only known afterwards.
    const int base =
        static_cast<int>(function_->locals.size() + function_->reg_types.size());
    return -8 * (base + static_cast<int>(slot) + 1);
}

int NativeBackend::scratch_offset(u32 index) const {
    const int base = static_cast<int>(function_->locals.size() +
                                      function_->reg_types.size() +
                                      escapes_.frame_slots_needed);
    return -8 * (base + static_cast<int>(index) + 1);
}

void NativeBackend::load(const std::string &reg, int offset) {
    out_ << "\tmovq\t" << offset << "(%rbp), " << reg << "\n";
}
void NativeBackend::store(const std::string &reg, int offset) {
    out_ << "\tmovq\t" << reg << ", " << offset << "(%rbp)\n";
}
void NativeBackend::load_sse(const std::string &reg, int offset) {
    out_ << "\tmovsd\t" << offset << "(%rbp), " << reg << "\n";
}
void NativeBackend::store_sse(const std::string &reg, int offset) {
    out_ << "\tmovsd\t" << reg << ", " << offset << "(%rbp)\n";
}

void NativeBackend::checked_op(const char *mnemonic, const char *trap_label) {
    // Integer arithmetic traps on overflow, so every operation that can
    // overflow is followed by a jump to a stub that panics. One stub per
    // message per function is enough, so the cost on the common path is a
    // single not-taken branch.
    const std::string label = std::string(trap_label) + "_f" + std::to_string(function_index_);
    out_ << "\t" << mnemonic << "\n";
    out_ << "\tjo\t" << label << "\n";
    if (std::find(traps_.begin(), traps_.end(), std::string(trap_label)) == traps_.end()) {
        traps_.push_back(trap_label);
    }
}

// ---------------------------------------------------------------------------
// Aggregates
// ---------------------------------------------------------------------------

u32 NativeBackend::copy_decl_for(const Type *type) const {
    if (!type || type->kind != TypeKind::Struct) return 0xFFFFFFFFu;
    if (type->decl >= types_.struct_count()) return 0xFFFFFFFFu;
    // Objects have identity, so copying one would be wrong.
    if (types_.struct_at(type->decl).is_reference) return 0xFFFFFFFFu;
    return type->decl;
}

int NativeBackend::copy_if_value_struct(const Type *type, int source_offset, Reg source) {
    const u32 decl = copy_decl_for(type);
    if (decl == 0xFFFFFFFFu) return source_offset;

    // Copy elision: a freshly built aggregate with exactly one reader is
    // unaliased, so the reader can take the block rather than duplicate it.
    if (source != kNoReg && escapes_.can_take_ownership(source)) {
        ++elided_copies_;
        return source_offset;
    }

    if (std::find(copy_helpers_.begin(), copy_helpers_.end(), decl) == copy_helpers_.end()) {
        copy_helpers_.push_back(decl);
    }

    const int destination = scratch_offset(scratch_used_++);
    scratch_high_water_ = std::max(scratch_high_water_, scratch_used_);

    load("%rdi", source_offset);
    out_ << "\tcall\tppcopy" << decl << "\n";
    store("%rax", destination);
    return destination;
}

void NativeBackend::emit_copy_helpers() {
    // Each helper allocates a fresh block and copies the source into it,
    // recursing into fields that are themselves value structs. Emitting one
    // function per type keeps the copy out of every call site.
    for (std::size_t i = 0; i < copy_helpers_.size(); ++i) {
        const u32 decl = copy_helpers_[i];
        const StructInfo &info = types_.struct_at(decl);
        const std::size_t fields = info.fields.size();

        out_ << "\n\t.p2align 4\n";
        out_ << "ppcopy" << decl << ":\n";
        out_ << "\t.cfi_startproc\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
        out_ << "\tpushq\t%rbx\n\tpushq\t%r12\n";
        out_ << "\tsubq\t$8, %rsp\n";  // keep %rsp 16-byte aligned at the call
        out_ << "\tmovq\t%rdi, %rbx\n";  // source

        out_ << "\tmovq\t$" << (fields ? fields * 8 : 8) << ", %rdi\n";
        out_ << "\tcall\tpp_object_alloc\n";
        out_ << "\tmovq\t%rax, %r12\n";  // destination

        // A null source means the value was never initialized; the zeroed block
        // from pp_object_alloc is already the right answer.
        out_ << "\ttestq\t%rbx, %rbx\n";
        out_ << "\tje\t.Lcopy" << decl << "_done\n";

        for (std::size_t f = 0; f < fields; ++f) {
            out_ << "\tmovq\t" << (f * 8) << "(%rbx), %rax\n";
            out_ << "\tmovq\t%rax, " << (f * 8) << "(%r12)\n";
        }

        for (std::size_t f = 0; f < fields; ++f) {
            const Type *field = info.fields[f].type;
            const u32 nested = copy_decl_for(field);
            if (nested == 0xFFFFFFFFu) continue;
            if (std::find(copy_helpers_.begin(), copy_helpers_.end(), nested) ==
                copy_helpers_.end()) {
                copy_helpers_.push_back(nested);
            }
            // A shallow copy would leave both structs sharing this field's
            // block, so nested value fields are copied in turn.
            out_ << "\tmovq\t" << (f * 8) << "(%r12), %rdi\n";
            out_ << "\ttestq\t%rdi, %rdi\n";
            out_ << "\tje\t.Lcopy" << decl << "_skip" << f << "\n";
            out_ << "\tcall\tppcopy" << nested << "\n";
            out_ << "\tmovq\t%rax, " << (f * 8) << "(%r12)\n";
            out_ << ".Lcopy" << decl << "_skip" << f << ":\n";
        }

        out_ << ".Lcopy" << decl << "_done:\n";
        out_ << "\tmovq\t%r12, %rax\n";
        out_ << "\taddq\t$8, %rsp\n";
        out_ << "\tpopq\t%r12\n\tpopq\t%rbx\n";
        out_ << "\tpopq\t%rbp\n\tret\n";
        out_ << "\t.cfi_endproc\n";
    }
}

// ---------------------------------------------------------------------------
// Binary operations
// ---------------------------------------------------------------------------

void NativeBackend::emit_binary(const MirFunction &fn, const MirInst &in) {
    const TypeKind operand = static_cast<TypeKind>(in.imm);
    const int dest = register_offset(in.dest);
    const int lhs = register_offset(in.a);
    const int rhs = register_offset(in.b);
    (void)fn;

    // --- text -------------------------------------------------------------
    if (operand == TypeKind::Str) {
        const char *helper = nullptr;
        bool negate = false;
        const char *setcc = nullptr;

        switch (in.binary_op) {
            case BinaryOp::Add: helper = "pp_concat"; break;
            case BinaryOp::Equal: helper = "pp_str_eq"; break;
            case BinaryOp::NotEqual: helper = "pp_str_eq"; negate = true; break;
            case BinaryOp::Less: helper = "pp_str_cmp"; setcc = "setl"; break;
            case BinaryOp::LessEqual: helper = "pp_str_cmp"; setcc = "setle"; break;
            case BinaryOp::Greater: helper = "pp_str_cmp"; setcc = "setg"; break;
            case BinaryOp::GreaterEqual: helper = "pp_str_cmp"; setcc = "setge"; break;
            default: helper = "pp_concat"; break;
        }

        load("%rdi", lhs);
        load("%rsi", rhs);
        out_ << "\tcall\t" << helper << "\n";
        if (setcc) {
            // pp_str_cmp returns -1, 0, or 1; the comparison turns that into a
            // boolean without a second call.
            out_ << "\tcmpq\t$0, %rax\n";
            out_ << "\t" << setcc << "\t%al\n";
            out_ << "\tmovzbq\t%al, %rax\n";
        } else if (negate) {
            out_ << "\txorq\t$1, %rax\n";
        }
        store("%rax", dest);
        return;
    }

    // --- float ------------------------------------------------------------
    if (operand == TypeKind::Float) {
        load_sse("%xmm0", lhs);
        load_sse("%xmm1", rhs);

        switch (in.binary_op) {
            case BinaryOp::Add: out_ << "\taddsd\t%xmm1, %xmm0\n"; store_sse("%xmm0", dest); return;
            case BinaryOp::Subtract: out_ << "\tsubsd\t%xmm1, %xmm0\n"; store_sse("%xmm0", dest); return;
            case BinaryOp::Multiply: out_ << "\tmulsd\t%xmm1, %xmm0\n"; store_sse("%xmm0", dest); return;
            case BinaryOp::Divide: out_ << "\tdivsd\t%xmm1, %xmm0\n"; store_sse("%xmm0", dest); return;
            default: break;
        }

        // Comparisons. ucomisd sets the unsigned flags, so the below/above
        // conditions are the right ones even for signed floating point.
        const char *setcc = "sete";
        bool swap = false;
        switch (in.binary_op) {
            case BinaryOp::Equal: setcc = "sete"; break;
            case BinaryOp::NotEqual: setcc = "setne"; break;
            case BinaryOp::Less: setcc = "seta"; swap = true; break;
            case BinaryOp::LessEqual: setcc = "setae"; swap = true; break;
            case BinaryOp::Greater: setcc = "seta"; break;
            case BinaryOp::GreaterEqual: setcc = "setae"; break;
            default: break;
        }
        if (swap) out_ << "\tucomisd\t%xmm0, %xmm1\n";
        else out_ << "\tucomisd\t%xmm1, %xmm0\n";
        out_ << "\t" << setcc << "\t%al\n";
        if (in.binary_op == BinaryOp::Equal) {
            // An unordered comparison must not report equal, and ucomisd sets
            // the parity flag for NaN.
            out_ << "\tsetnp\t%cl\n\tandb\t%cl, %al\n";
        } else if (in.binary_op == BinaryOp::NotEqual) {
            out_ << "\tsetp\t%cl\n\torb\t%cl, %al\n";
        }
        out_ << "\tmovzbq\t%al, %rax\n";
        store("%rax", dest);
        return;
    }

    // --- integer and boolean ----------------------------------------------
    load("%rax", lhs);
    load("%rcx", rhs);

    switch (in.binary_op) {
        case BinaryOp::Add:
            if (options_.unchecked_arithmetic) out_ << "\taddq\t%rcx, %rax\n";
            else checked_op("addq\t%rcx, %rax", ".Ltrap_add");
            break;
        case BinaryOp::Subtract:
            if (options_.unchecked_arithmetic) out_ << "\tsubq\t%rcx, %rax\n";
            else checked_op("subq\t%rcx, %rax", ".Ltrap_sub");
            break;
        case BinaryOp::Multiply:
            if (options_.unchecked_arithmetic) out_ << "\timulq\t%rcx, %rax\n";
            else checked_op("imulq\t%rcx, %rax", ".Ltrap_mul");
            break;
        case BinaryOp::Divide:
        case BinaryOp::Modulo:
            // Division by zero and INT64_MIN / -1 both fault on x86, so both
            // operands go to the runtime helper that reports them properly.
            out_ << "\tmovq\t%rax, %rdi\n\tmovq\t%rcx, %rsi\n";
            out_ << "\tcall\t"
                 << (in.binary_op == BinaryOp::Divide ? "pp_div_i64" : "pp_mod_i64") << "\n";
            break;

        case BinaryOp::BitAnd: out_ << "\tandq\t%rcx, %rax\n"; break;
        case BinaryOp::BitOr: out_ << "\torq\t%rcx, %rax\n"; break;
        case BinaryOp::BitXor: out_ << "\txorq\t%rcx, %rax\n"; break;
        case BinaryOp::ShiftLeft:
        case BinaryOp::ShiftRight:
            // The shift amount is range-checked, which x86 will not do.
            out_ << "\tmovq\t%rax, %rdi\n\tmovq\t%rcx, %rsi\n";
            out_ << "\tcall\t"
                 << (in.binary_op == BinaryOp::ShiftLeft ? "pp_shl_i64" : "pp_shr_i64") << "\n";
            break;

        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual: {
            const char *setcc = "sete";
            switch (in.binary_op) {
                case BinaryOp::Equal: setcc = "sete"; break;
                case BinaryOp::NotEqual: setcc = "setne"; break;
                case BinaryOp::Less: setcc = "setl"; break;
                case BinaryOp::LessEqual: setcc = "setle"; break;
                case BinaryOp::Greater: setcc = "setg"; break;
                default: setcc = "setge"; break;
            }
            out_ << "\tcmpq\t%rcx, %rax\n";
            out_ << "\t" << setcc << "\t%al\n";
            out_ << "\tmovzbq\t%al, %rax\n";
            break;
        }

        // The MIR builder turns these into branches, so they do not reach here.
        case BinaryOp::And: out_ << "\tandq\t%rcx, %rax\n"; break;
        case BinaryOp::Or: out_ << "\torq\t%rcx, %rax\n"; break;
    }
    store("%rax", dest);
}

}  // namespace ppc
