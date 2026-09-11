#include <algorithm>
#include <cstdio>

#include "ppc/codegen/native_backend.hpp"
#include "ppc/sema/builtins.hpp"

namespace ppc {

namespace {
const char *const kIntegerParams[6] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

void NativeBackend::emit_function(const MirFunction &fn, std::size_t index) {
    function_ = &fn;
    function_index_ = index;
    scratch_used_ = 0;
    scratch_high_water_ = 0;
    traps_.clear();

    // Decide which allocations can live in the frame before emitting anything,
    // so their storage offsets are fixed while the body is generated.
    escapes_ = analyze_escapes(fn, types_);
    total_allocations_ += escapes_.total_allocations;
    promoted_allocations_ += escapes_.promoted_allocations;

    // The body is emitted into a scratch stream first, because the frame size
    // depends on how many copy temporaries the body ends up needing, and the
    // prologue has to come before all of it.
    std::ostringstream saved;
    saved.swap(out_);

    for (const MirBlock &block : fn.blocks) {
        if (!block.reachable) continue;
        out_ << block_label(block.id) << ":\n";
        // Copy temporaries are scoped to a block; reusing them across blocks
        // would need liveness analysis this backend deliberately avoids.
        scratch_used_ = 0;
        for (const MirInst &instruction : block.instructions) {
            emit_instruction(fn, instruction);
        }
    }

    std::string body = out_.str();
    out_.swap(saved);

    const std::size_t slots = fn.locals.size() + fn.reg_types.size() +
                              escapes_.frame_slots_needed + scratch_high_water_;
    // Keep %rsp 16-byte aligned at every call, as the ABI requires.
    int frame = static_cast<int>(slots * 8);
    frame = (frame + 15) & ~15;

    const std::string label = function_label(index);
    out_ << "\n\t.p2align 4\n";
    out_ << "\t.type\t" << label << ", @function\n";
    out_ << label << ":\n";
    out_ << "\t.cfi_startproc\n";
    out_ << "\tpushq\t%rbp\n";
    out_ << "\t.cfi_def_cfa_offset 16\n";
    out_ << "\t.cfi_offset 6, -16\n";
    out_ << "\tmovq\t%rsp, %rbp\n";
    out_ << "\t.cfi_def_cfa_register 6\n";
    if (frame > 0) out_ << "\tsubq\t$" << frame << ", %rsp\n";

    // Incoming parameters are spilled into their local slots first, which is
    // what lets every later reference be a plain memory access. This has to
    // happen before the frame is zeroed, because the zeroing uses %rdi, %rcx,
    // and %rax, three of which carry incoming arguments.
    int gp = 0;
    int sse = 0;
    for (u32 i = 0; i < fn.param_count; ++i) {
        const Type *type = fn.locals[i].type;
        if (type && type->kind == TypeKind::Float) {
            if (sse < 8) out_ << "\tmovsd\t%xmm" << sse << ", " << local_offset(i) << "(%rbp)\n";
            ++sse;
        } else {
            if (gp < 6) store(kIntegerParams[gp], local_offset(i));
            ++gp;
        }
    }

    // Zero everything that is not a parameter, so an uninitialized slot reads
    // as 0 rather than as stack garbage. Parameters occupy the highest slots,
    // so the remaining ones form one contiguous run at the bottom of the frame.
    if (slots > fn.param_count) {
        const std::size_t count = slots - fn.param_count;
        out_ << "\tleaq\t" << -static_cast<int>(slots * 8) << "(%rbp), %rdi\n";
        out_ << "\tmovq\t$" << count << ", %rcx\n";
        out_ << "\txorq\t%rax, %rax\n";
        out_ << "\trep stosq\n";
    }

    if (gp > 6 || sse > 8) {
        diagnostics_
            .error(Code::BackendUnavailable,
                   "the native backend supports at most 6 integer and 8 float parameters")
            .label(fn.span)
            .with_help("group the parameters into a struct, or use --backend=c");
    }

    out_ << body;

    out_ << epilogue_label() << ":\n";
    out_ << "\tmovq\t%rbp, %rsp\n";
    out_ << "\tpopq\t%rbp\n";
    out_ << "\t.cfi_def_cfa 7, 8\n";
    out_ << "\tret\n";

    // Overflow trap stubs, shared across every site in the function that needs
    // the same message.
    for (const std::string &trap : traps_) {
        const char *message = "integer overflow";
        if (trap == ".Ltrap_add") message = "integer overflow in addition";
        else if (trap == ".Ltrap_sub") message = "integer overflow in subtraction";
        else if (trap == ".Ltrap_mul") message = "integer overflow in multiplication";
        else if (trap == ".Ltrap_neg") message = "integer overflow in negation";

        const u32 index_of_message = intern_string(message);
        out_ << trap << "_f" << function_index_ << ":\n";
        out_ << "\tleaq\t.Lstr" << index_of_message << "(%rip), %rdi\n";
        out_ << "\tmovb\t$0, %al\n";
        out_ << "\tcall\tpp_panic\n";
    }

    out_ << "\t.cfi_endproc\n";
    out_ << "\t.size\t" << label << ", .-" << label << "\n";

    function_ = nullptr;
}

// ---------------------------------------------------------------------------
// Data and entry
// ---------------------------------------------------------------------------

void NativeBackend::emit_string_pool() {
    if (strings_.empty()) return;
    out_ << "\n\t.section\t.rodata\n";
    for (std::size_t i = 0; i < strings_.size(); ++i) {
        out_ << ".Lstr" << i << ":\n\t.string\t\"";
        for (unsigned char c : strings_[i]) {
            switch (c) {
                case '"': out_ << "\\\""; break;
                case '\\': out_ << "\\\\"; break;
                case '\n': out_ << "\\n"; break;
                case '\t': out_ << "\\t"; break;
                case '\r': out_ << "\\r"; break;
                default:
                    if (c < 0x20 || c == 0x7F) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof(buffer), "\\%03o", c);
                        out_ << buffer;
                    } else {
                        out_ << static_cast<char>(c);
                    }
            }
        }
        out_ << "\"\n";
    }
    out_ << "\t.text\n";
}

void NativeBackend::emit_function_table(const MirProgram &program) {
    // A function value is an index into this table, so an indirect call is an
    // ordinary load followed by a call through the register.
    // .data.rel.ro, not .rodata: each entry is a code address that the dynamic
    // linker has to relocate, and a PIE cannot carry relocations into a section
    // that is mapped read-only from the start.
    out_ << "\n\t.section\t.data.rel.ro,\"aw\",@progbits\n";
    out_ << "\t.align\t8\n";
    out_ << "pp_fn_table:\n";
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const MirFunction *fn = program.functions[i];
        const std::string symbol = fn->is_extern_native ? std::string(fn->native_symbol)
                                                        : function_label(i);
        out_ << "\t.quad\t" << symbol << "\n";
    }
    // Keeps the symbol well formed for a program that defines no functions.
    out_ << "\t.quad\t0\n";
    out_ << "\t.text\n";
}

void NativeBackend::emit_entry(const MirProgram &program) {
    out_ << "\n\t.globl\tmain\n";
    out_ << "\t.type\tmain, @function\n";
    out_ << "main:\n";
    out_ << "\t.cfi_startproc\n";
    out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
    out_ << "\tsubq\t$16, %rsp\n";
    // argc and argv arrive in %rdi and %rsi and are still there, because
    // nothing above touches them. They pass straight through to the runtime so
    // `arg` and `arg_count` work.
    out_ << "\tmovb\t$0, %al\n\tcall\tpp_runtime_init\n";
    if (program.entry < program.functions.size()) {
        out_ << "\tmovb\t$0, %al\n\tcall\t" << function_label(program.entry) << "\n";
    }
    out_ << "\tmovb\t$0, %al\n\tcall\tpp_runtime_cleanup\n";
    out_ << "\txorl\t%eax, %eax\n";
    out_ << "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\tret\n";
    out_ << "\t.cfi_endproc\n";
    out_ << "\t.size\tmain, .-main\n";
}

bool NativeBackend::emit(const MirProgram &program, const CodegenOptions &options,
                         std::string &out) {
    program_ = &program;
    options_ = options;
    strings_.clear();
    string_index_.clear();
    copy_helpers_.clear();
    out_.str({});
    out_.clear();

    if (!program.injections.empty()) {
        diagnostics_
            .warning(Code::BackendUnavailable,
                     "the native backend cannot inline foreign source blocks")
            .label(program.injections.front().span)
            .with_help("use --backend=c when a program injects foreign source");
    }

    std::ostringstream header;
    header << "# Generated by ppc, the PunPun compiler.\n";
    if (!options.source_name.empty()) header << "# Source: " << options.source_name << "\n";
    header << "# Target: x86-64 System V\n";
    header << "# Arithmetic is "
           << (options.unchecked_arithmetic ? "unchecked (--unchecked)"
                                            : "checked, per the language spec")
           << ".\n";
    header << "\t.text\n";

    // Bodies are emitted first so the string pool and copy helpers are complete
    // before either is written out.
    std::ostringstream bodies;
    bodies.swap(out_);
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        if (program.functions[i]->is_extern_native) continue;
        emit_function(*program.functions[i], i);
    }
    emit_copy_helpers();
    emit_entry(program);
    bodies.swap(out_);

    // Trap stubs reference per-function labels, so the jump targets emitted by
    // checked_op are rewritten here to match the stub names.
    std::string text = bodies.str();

    out_.str({});
    out_.clear();
    emit_string_pool();
    emit_function_table(program);
    const std::string pool = out_.str();

    // Without this section the linker assumes the object wants an executable
    // stack and warns about it. Nothing ppc emits needs one.
    const std::string footer = "\n\t.section\t.note.GNU-stack,\"\",@progbits\n";

    out = header.str() + pool + text + footer;
    return !diagnostics_.has_errors();
}

}  // namespace ppc
