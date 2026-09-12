#include <algorithm>
#include <cstdio>

#include "ppc/codegen/native_backend.hpp"
#include "ppc/sema/builtins.hpp"

namespace ppc {

namespace {
const char *const kIntegerParams[6] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
const char *const kAllocatedRegisters[5] = {"%rbx", "%r12", "%r13", "%r14", "%r15"};
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
    allocate_registers(fn);

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

    const std::size_t base_slots = fn.locals.size() + 1 + fn.reg_types.size() +
                                   escapes_.frame_slots_needed + scratch_high_water_;
    const std::size_t slots = base_slots + used_register_slots_.size();
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
    int stack = 0;
    for (u32 i = 0; i < fn.param_count; ++i) {
        const Type *type = fn.locals[i].type;
        if (type && type->kind == TypeKind::Float) {
            if (sse < 8) {
                out_ << "\tmovsd\t%xmm" << sse << ", " << local_offset(i) << "(%rbp)\n";
                ++sse;
            } else {
                // Stack arguments begin above the saved %rbp and return address.
                // Every PunPun ABI value occupies one eightbyte, including f64.
                out_ << "\tmovsd\t" << (16 + stack * 8) << "(%rbp), %xmm15\n";
                out_ << "\tmovsd\t%xmm15, " << local_offset(i) << "(%rbp)\n";
                ++stack;
            }
        } else {
            if (gp < 6) {
                store(kIntegerParams[gp], local_offset(i));
                ++gp;
            } else {
                out_ << "\tmovq\t" << (16 + stack * 8) << "(%rbp), %rax\n";
                store("%rax", local_offset(i));
                ++stack;
            }
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

    // Preserve every callee-saved machine register selected by linear scan.
    // Their save area lives after ordinary frame slots and copy scratch.
    for (std::size_t i = 0; i < used_register_slots_.size(); ++i) {
        const int offset = -8 * static_cast<int>(base_slots + i + 1);
        store(kAllocatedRegisters[used_register_slots_[i]], offset);
    }

    // Native uses %r10 as an internal hidden closure register. It is not part
    // of PunPun's public ABI, so source-visible parameter registers are unchanged.
    store("%r10", closure_offset());


    out_ << body;

    out_ << epilogue_label() << ":\n";
    for (std::size_t i = used_register_slots_.size(); i-- > 0;) {
        const int offset = -8 * static_cast<int>(base_slots + i + 1);
        load(kAllocatedRegisters[used_register_slots_[i]], offset);
    }
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
    // A closure stores a target index into this table. Indirect calls load that
    // target and pass the closure handle separately as the hidden environment.
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

void NativeBackend::emit_task_trampolines(const MirProgram &program) {
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const MirFunction &fn = *program.functions[index];
        if (!fn.is_async || fn.is_extern_native) continue;

        const std::size_t context_bytes = static_cast<std::size_t>(fn.param_count) * 8u;
        int context_frame = static_cast<int>(context_bytes);
        context_frame = (context_frame + 15) & ~15;

        // Spawn wrapper. It has the same ABI as the source async function, but
        // snapshots its arguments into a plain eightbyte context and asks the
        // runtime to own/copy that context before starting a worker.
        out_ << "\n\t.p2align 4\n";
        out_ << "pptask_spawn" << index << ":\n";
        out_ << "\t.cfi_startproc\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
        if (context_frame) out_ << "\tsubq\t$" << context_frame << ", %rsp\n";

        int gp = 0;
        int sse = 0;
        int stack = 0;
        for (u32 p = 0; p < fn.param_count; ++p) {
            const Type *type = fn.locals[p].type;
            const int destination = -static_cast<int>(context_bytes) + static_cast<int>(p * 8u);
            if (type && type->kind == TypeKind::Float) {
                if (sse < 8) {
                    out_ << "\tmovsd\t%xmm" << sse++ << ", " << destination << "(%rbp)\n";
                } else {
                    out_ << "\tmovq\t" << (16 + stack * 8) << "(%rbp), %rax\n";
                    out_ << "\tmovq\t%rax, " << destination << "(%rbp)\n";
                    ++stack;
                }
            } else {
                if (gp < 6) {
                    out_ << "\tmovq\t" << kIntegerParams[gp++] << ", " << destination << "(%rbp)\n";
                } else {
                    out_ << "\tmovq\t" << (16 + stack * 8) << "(%rbp), %rax\n";
                    out_ << "\tmovq\t%rax, " << destination << "(%rbp)\n";
                    ++stack;
                }
            }
        }

        out_ << "\tleaq\tpptask" << index << "(%rip), %rdi\n";
        if (context_bytes) {
            out_ << "\tleaq\t-" << context_bytes << "(%rbp), %rsi\n";
            out_ << "\tmovq\t$" << context_bytes << ", %rdx\n";
        } else {
            out_ << "\txorl\t%esi, %esi\n\txorl\t%edx, %edx\n";
        }
        out_ << "\tmovb\t$0, %al\n\tcall\tpp_task_spawn\n";
        out_ << "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\tret\n";
        out_ << "\t.cfi_endproc\n";

        // Worker entry. The runtime passes the copied context pointer in %rdi;
        // remarshal it through the ordinary PunPun calling convention so the
        // async body is exactly the same generated function as a synchronous
        // direct call would use.
        out_ << "\n\t.p2align 4\n";
        out_ << "pptask" << index << ":\n";
        out_ << "\t.cfi_startproc\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
        out_ << "\tmovq\t%rdi, %r11\n";

        gp = 0;
        sse = 0;
        std::vector<bool> on_stack(fn.param_count, false);
        std::size_t stack_count = 0;
        for (u32 p = 0; p < fn.param_count; ++p) {
            const Type *type = fn.locals[p].type;
            if (type && type->kind == TypeKind::Float) {
                if (sse < 8) ++sse;
                else { on_stack[p] = true; ++stack_count; }
            } else {
                if (gp < 6) ++gp;
                else { on_stack[p] = true; ++stack_count; }
            }
        }
        const std::size_t pad = (stack_count & 1u) ? 8u : 0u;
        if (pad) out_ << "\tsubq\t$8, %rsp\n";
        for (std::size_t p = fn.param_count; p-- > 0;) {
            if (!on_stack[p]) continue;
            out_ << "\tmovq\t" << (p * 8u) << "(%r11), %rax\n";
            out_ << "\tpushq\t%rax\n";
        }

        gp = 0;
        sse = 0;
        for (u32 p = 0; p < fn.param_count; ++p) {
            if (on_stack[p]) continue;
            const Type *type = fn.locals[p].type;
            if (type && type->kind == TypeKind::Float) {
                out_ << "\tmovsd\t" << (p * 8u) << "(%r11), %xmm" << sse++ << "\n";
            } else {
                out_ << "\tmovq\t" << (p * 8u) << "(%r11), " << kIntegerParams[gp++] << "\n";
            }
        }
        out_ << "\txorl\t%r10d, %r10d\n";
        out_ << "\tmovb\t$" << sse << ", %al\n";
        out_ << "\tcall\t" << function_label(index) << "\n";
        const std::size_t stack_bytes = stack_count * 8u + pad;
        if (stack_bytes) out_ << "\taddq\t$" << stack_bytes << ", %rsp\n";

        if (!fn.result || fn.result->kind == TypeKind::Void) {
            out_ << "\txorl\t%eax, %eax\n";
        } else if (fn.result->kind == TypeKind::Float) {
            out_ << "\tmovq\t%xmm0, %rax\n";
        } else if (fn.result->kind == TypeKind::Bool) {
            out_ << "\tmovzbq\t%al, %rax\n";
        }
        out_ << "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\tret\n";
        out_ << "\t.cfi_endproc\n";
    }
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
        out_ << "\txorl\t%r10d, %r10d\n";
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
    emit_task_trampolines(program);
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
