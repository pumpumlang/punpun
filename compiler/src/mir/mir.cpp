#include "ppc/mir/mir.hpp"

#include <algorithm>
#include <sstream>

namespace ppc {

BlockId MirFunction::add_block() {
    MirBlock block;
    block.id = static_cast<BlockId>(blocks.size());
    blocks.push_back(std::move(block));
    return blocks.back().id;
}

Reg MirFunction::add_reg(const Type *type) {
    reg_types.push_back(type);
    return static_cast<Reg>(reg_types.size() - 1);
}

void MirFunction::compute_cfg() {
    for (MirBlock &block : blocks) {
        block.predecessors.clear();
        block.successors.clear();
        block.reachable = false;
    }

    for (MirBlock &block : blocks) {
        if (block.instructions.empty()) continue;
        const MirInst &last = block.instructions.back();
        if (last.op == MirOp::Jump) {
            block.successors.push_back(last.then_block);
        } else if (last.op == MirOp::Branch) {
            block.successors.push_back(last.then_block);
            if (last.else_block != last.then_block) block.successors.push_back(last.else_block);
        }
    }
    for (MirBlock &block : blocks) {
        for (BlockId successor : block.successors) {
            if (successor < blocks.size()) blocks[successor].predecessors.push_back(block.id);
        }
    }

    // Depth-first reachability from the entry.
    std::vector<BlockId> stack{entry_block};
    while (!stack.empty()) {
        const BlockId id = stack.back();
        stack.pop_back();
        if (id >= blocks.size() || blocks[id].reachable) continue;
        blocks[id].reachable = true;
        for (BlockId successor : blocks[id].successors) stack.push_back(successor);
    }
}

std::string MirFunction::verify() const {
    for (const MirBlock &block : blocks) {
        if (!block.reachable) continue;
        if (block.instructions.empty()) {
            return "block " + std::to_string(block.id) + " is empty";
        }
        if (!block.instructions.back().is_terminator()) {
            return "block " + std::to_string(block.id) + " does not end in a terminator";
        }
        for (std::size_t i = 0; i + 1 < block.instructions.size(); ++i) {
            if (block.instructions[i].is_terminator()) {
                return "block " + std::to_string(block.id) + " has a terminator at instruction " +
                       std::to_string(i) + ", before the end";
            }
        }
        for (const MirInst &instruction : block.instructions) {
            if (instruction.op == MirOp::Jump || instruction.op == MirOp::Branch) {
                if (instruction.then_block >= blocks.size()) {
                    return "block " + std::to_string(block.id) + " jumps to a block that does not exist";
                }
                if (instruction.op == MirOp::Branch && instruction.else_block >= blocks.size()) {
                    return "block " + std::to_string(block.id) + " branches to a block that does not exist";
                }
            }
            if (instruction.dest != kNoReg && instruction.dest >= reg_types.size()) {
                return "instruction defines an out-of-range register";
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Textual dump
// ---------------------------------------------------------------------------

namespace {

const char *op_name(MirOp op) {
    switch (op) {
        case MirOp::ConstInt: return "const.int";
        case MirOp::ConstFloat: return "const.float";
        case MirOp::ConstStr: return "const.str";
        case MirOp::ConstBool: return "const.bool";
        case MirOp::LoadLocal: return "load";
        case MirOp::StoreLocal: return "store";
        case MirOp::LocalAddr: return "addr";
        case MirOp::Binary: return "bin";
        case MirOp::Unary: return "un";
        case MirOp::Call: return "call";
        case MirOp::CallIndirect: return "call.indirect";
        case MirOp::CallBuiltin: return "call.builtin";
        case MirOp::MakeStruct: return "struct.new";
        case MirOp::GetField: return "field.get";
        case MirOp::SetField: return "field.set";
        case MirOp::MakeEnum: return "enum.new";
        case MirOp::EnumTag: return "enum.tag";
        case MirOp::EnumPayload: return "enum.payload";
        case MirOp::MakeList: return "list.new";
        case MirOp::GetIndex: return "index.get";
        case MirOp::SetIndex: return "index.set";
        case MirOp::Deref: return "deref";
        case MirOp::StoreDeref: return "deref.set";
        case MirOp::Await: return "await";
        case MirOp::Drop: return "drop";
        case MirOp::Jump: return "jump";
        case MirOp::Branch: return "branch";
        case MirOp::Return: return "return";
    }
    return "?";
}

std::string reg_name(Reg reg) {
    return reg == kNoReg ? std::string("_") : "%" + std::to_string(reg);
}

}  // namespace

std::string dump_mir(const MirProgram &program, const TypeContext &types) {
    std::ostringstream out;

    for (const MirFunction *fn : program.functions) {
        if (fn->is_extern_native) {
            out << "extern native " << fn->name << " -> " << types.describe(fn->result) << "\n\n";
            continue;
        }

        out << "fn " << fn->name << " -> " << types.describe(fn->result);
        if (fn->is_entry) out << "  ; entry point";
        out << "\n";

        for (std::size_t i = 0; i < fn->locals.size(); ++i) {
            out << "  local $" << i << ": " << types.describe(fn->locals[i].type);
            if (fn->locals[i].is_parameter) out << "  ; parameter";
            if (fn->locals[i].address_taken) out << "  ; address taken";
            out << "\n";
        }

        for (const MirBlock &block : fn->blocks) {
            if (!block.reachable) {
                out << "  bb" << block.id << ":  ; unreachable\n";
                continue;
            }
            out << "  bb" << block.id << ":";
            if (!block.predecessors.empty()) {
                out << "  ; preds:";
                for (BlockId predecessor : block.predecessors) out << " bb" << predecessor;
            }
            out << "\n";

            for (const MirInst &instruction : block.instructions) {
                out << "    ";
                if (instruction.dest != kNoReg) out << reg_name(instruction.dest) << " = ";
                out << op_name(instruction.op);

                switch (instruction.op) {
                    case MirOp::ConstInt: out << " " << instruction.imm; break;
                    case MirOp::ConstFloat: out << " " << instruction.fimm; break;
                    case MirOp::ConstBool: out << " " << (instruction.imm ? "true" : "false"); break;
                    case MirOp::ConstStr: out << " \"" << types.interner().text(instruction.text) << "\""; break;
                    case MirOp::LoadLocal: out << " $" << instruction.index; break;
                    case MirOp::LocalAddr: out << " $" << instruction.index; break;
                    case MirOp::StoreLocal: out << " $" << instruction.index << ", " << reg_name(instruction.a); break;
                    case MirOp::Binary:
                        out << " " << binary_op_spelling(instruction.binary_op) << " "
                            << reg_name(instruction.a) << ", " << reg_name(instruction.b);
                        break;
                    case MirOp::Unary:
                        out << " " << unary_op_spelling(instruction.unary_op) << " "
                            << reg_name(instruction.a);
                        break;
                    case MirOp::CallIndirect:
                        // The callee is a register, so it prints like any other
                        // operand ahead of the argument list.
                        out << " " << reg_name(instruction.a);
                        for (Reg argument : instruction.args) {
                            out << " " << reg_name(argument);
                        }
                        break;
                    case MirOp::Call:
                    case MirOp::CallBuiltin:
                    case MirOp::MakeStruct:
                    case MirOp::MakeEnum:
                    case MirOp::MakeList: {
                        out << " #" << instruction.target;
                        if (instruction.op == MirOp::MakeEnum) out << " variant " << instruction.index;
                        out << " (";
                        for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                            if (i) out << ", ";
                            out << reg_name(instruction.args[i]);
                        }
                        out << ")";
                        break;
                    }
                    case MirOp::GetField: out << " " << reg_name(instruction.a) << "." << instruction.index; break;
                    case MirOp::SetField:
                        out << " " << reg_name(instruction.a) << "." << instruction.index << ", "
                            << reg_name(instruction.b);
                        break;
                    case MirOp::EnumTag: out << " " << reg_name(instruction.a); break;
                    case MirOp::EnumPayload:
                        out << " " << reg_name(instruction.a) << " variant " << instruction.index
                            << " slot " << instruction.slot;
                        break;
                    case MirOp::GetIndex:
                        out << " " << reg_name(instruction.a) << "[" << reg_name(instruction.b) << "]";
                        break;
                    case MirOp::SetIndex:
                        out << " " << reg_name(instruction.a) << "[" << reg_name(instruction.b)
                            << "], " << reg_name(instruction.c);
                        break;
                    case MirOp::Deref:
                    case MirOp::Await: out << " " << reg_name(instruction.a); break;
                    case MirOp::StoreDeref:
                        out << " " << reg_name(instruction.a) << ", " << reg_name(instruction.b);
                        break;
                    case MirOp::Drop: out << " $" << instruction.index; break;
                    case MirOp::Jump: out << " bb" << instruction.then_block; break;
                    case MirOp::Branch:
                        out << " " << reg_name(instruction.a) << " ? bb" << instruction.then_block
                            << " : bb" << instruction.else_block;
                        break;
                    case MirOp::Return:
                        if (instruction.a != kNoReg) out << " " << reg_name(instruction.a);
                        break;
                }
                out << "\n";
            }
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace ppc
