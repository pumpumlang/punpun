#include "ppc/codegen/bytecode.hpp"

#include <cstring>
#include <sstream>

namespace ppc {

const char *op_mnemonic(Op op) {
    switch (op) {
        case Op::Halt: return "halt";
        case Op::ConstInt: return "const.int";
        case Op::ConstFloat: return "const.float";
        case Op::ConstStr: return "const.str";
        case Op::ConstBool: return "const.bool";
        case Op::Move: return "move";
        case Op::CopyStruct: return "struct.copy";
        case Op::LoadLocal: return "load";
        case Op::StoreLocal: return "store";
        case Op::LocalAddr: return "addr";
        case Op::AddInt: return "add.i";
        case Op::SubInt: return "sub.i";
        case Op::MulInt: return "mul.i";
        case Op::DivInt: return "div.i";
        case Op::ModInt: return "mod.i";
        case Op::NegInt: return "neg.i";
        case Op::UncheckedAddInt: return "add.i.raw";
        case Op::UncheckedSubInt: return "sub.i.raw";
        case Op::UncheckedMulInt: return "mul.i.raw";
        case Op::AddFloat: return "add.f";
        case Op::SubFloat: return "sub.f";
        case Op::MulFloat: return "mul.f";
        case Op::DivFloat: return "div.f";
        case Op::NegFloat: return "neg.f";
        case Op::BitAnd: return "and";
        case Op::BitOr: return "or";
        case Op::BitXor: return "xor";
        case Op::ShiftLeft: return "shl";
        case Op::ShiftRight: return "shr";
        case Op::BitNot: return "not";
        case Op::EqInt: return "eq.i";
        case Op::NeInt: return "ne.i";
        case Op::LtInt: return "lt.i";
        case Op::LeInt: return "le.i";
        case Op::GtInt: return "gt.i";
        case Op::GeInt: return "ge.i";
        case Op::EqFloat: return "eq.f";
        case Op::NeFloat: return "ne.f";
        case Op::LtFloat: return "lt.f";
        case Op::LeFloat: return "le.f";
        case Op::GtFloat: return "gt.f";
        case Op::GeFloat: return "ge.f";
        case Op::EqStr: return "eq.s";
        case Op::NeStr: return "ne.s";
        case Op::LtStr: return "lt.s";
        case Op::LeStr: return "le.s";
        case Op::GtStr: return "gt.s";
        case Op::GeStr: return "ge.s";
        case Op::NotBool: return "not.b";
        case Op::ConcatStr: return "concat";
        case Op::Jump: return "jump";
        case Op::BranchTrue: return "branch";
        case Op::Call: return "call";
        case Op::CallIndirect: return "call.indirect";
        case Op::CallBuiltin: return "call.builtin";
        case Op::Return: return "return";
        case Op::ReturnVoid: return "return.void";
        case Op::MakeStruct: return "struct.new";
        case Op::GetField: return "field.get";
        case Op::SetField: return "field.set";
        case Op::MakeEnum: return "enum.new";
        case Op::EnumTag: return "enum.tag";
        case Op::EnumPayload: return "enum.payload";
        case Op::MakeList: return "list.new";
        case Op::GetIndex: return "index.get";
        case Op::GetSliceIndex: return "slice.get";
        case Op::SetIndex: return "index.set";
        case Op::Deref: return "deref";
        case Op::StoreDeref: return "deref.set";
        case Op::Spawn: return "spawn";
        case Op::Await: return "await";
    }
    return "?";
}

namespace {

/// True for opcodes whose `imm` names a code offset, so the disassembler can
/// print `-> 42` instead of a bare number.
bool is_jump(Op op) { return op == Op::Jump || op == Op::BranchTrue; }

void put_u32(std::string &out, u32 value) {
    out.append(reinterpret_cast<const char *>(&value), sizeof(value));
}
void put_u64(std::string &out, u64 value) {
    out.append(reinterpret_cast<const char *>(&value), sizeof(value));
}
void put_string(std::string &out, const std::string &text) {
    put_u32(out, static_cast<u32>(text.size()));
    out.append(text);
}

struct Reader {
    const std::string &bytes;
    std::size_t offset = 0;
    bool ok = true;

    bool need(std::size_t count) {
        if (offset + count > bytes.size()) {
            ok = false;
            return false;
        }
        return true;
    }
    u32 u32_value() {
        if (!need(4)) return 0;
        u32 value;
        std::memcpy(&value, bytes.data() + offset, 4);
        offset += 4;
        return value;
    }
    u64 u64_value() {
        if (!need(8)) return 0;
        u64 value;
        std::memcpy(&value, bytes.data() + offset, 8);
        offset += 8;
        return value;
    }
    std::string string_value() {
        const u32 length = u32_value();
        if (!need(length)) return {};
        std::string text = bytes.substr(offset, length);
        offset += length;
        return text;
    }
};

constexpr char kMagic[8] = {'P', 'P', 'C', 'B', 'C', '0', '0', '1'};

}  // namespace

std::string BytecodeProgram::disassemble() const {
    std::ostringstream out;
    out << "; ppc bytecode";
    if (!source_name.empty()) out << " for " << source_name;
    out << "\n; " << functions.size() << " function(s), " << code.size() << " instruction(s), "
        << strings.size() << " string constant(s)\n\n";

    if (!strings.empty()) {
        out << "; string pool\n";
        for (std::size_t i = 0; i < strings.size(); ++i) {
            out << ";   [" << i << "] \"" << strings[i] << "\"\n";
        }
        out << "\n";
    }

    for (std::size_t f = 0; f < functions.size(); ++f) {
        const BytecodeFunction &fn = functions[f];
        out << "fn #" << f << " " << fn.name;
        if (f == entry) out << "  ; entry point";
        out << "\n";
        out << ";   params " << fn.param_count << ", locals " << fn.local_count
            << ", registers " << fn.register_count << ", frame " << fn.frame_size << "\n";

        if (fn.is_extern_native) {
            out << "    <native " << fn.native_symbol << ">\n\n";
            continue;
        }

        for (u32 i = 0; i < fn.instruction_count; ++i) {
            const u32 address = fn.entry + i;
            const Instr &instruction = code[address];
            out << "  " << address << ":\t" << op_mnemonic(instruction.op);

            if (instruction.dest != 0xFFFFFFFFu &&
                instruction.op != Op::Jump && instruction.op != Op::BranchTrue) {
                out << " s" << instruction.dest;
            }
            if (instruction.a != 0xFFFFFFFFu) out << " s" << instruction.a;
            if (instruction.b != 0xFFFFFFFFu) out << " s" << instruction.b;
            if (instruction.c != 0xFFFFFFFFu) out << " s" << instruction.c;

            switch (instruction.op) {
                case Op::ConstStr:
                    out << " \"" << (static_cast<std::size_t>(instruction.imm) < strings.size()
                                         ? strings[instruction.imm]
                                         : std::string("?"))
                        << "\"";
                    break;
                case Op::ConstFloat: out << " " << instruction.fimm; break;
                case Op::Call:
                case Op::Spawn: out << " #" << instruction.imm; break;
                case Op::CallBuiltin: out << " builtin:" << instruction.imm; break;
                default:
                    if (is_jump(instruction.op)) out << " -> " << instruction.imm;
                    else if (instruction.imm != 0) out << " " << instruction.imm;
            }

            if (instruction.arg_count) {
                out << " (";
                for (u32 k = 0; k < instruction.arg_count; ++k) {
                    if (k) out << ", ";
                    out << "s" << arguments[instruction.arg_offset + k];
                }
                out << ")";
            }
            out << "\n";
        }
        out << "\n";
    }
    return out.str();
}

std::string BytecodeProgram::serialize() const {
    std::string out;
    out.append(kMagic, sizeof(kMagic));
    put_string(out, source_name);
    put_u32(out, entry);

    put_u32(out, static_cast<u32>(strings.size()));
    for (const std::string &text : strings) put_string(out, text);

    put_u32(out, static_cast<u32>(arguments.size()));
    for (u32 value : arguments) put_u32(out, value);

    put_u32(out, static_cast<u32>(layouts.size()));
    for (const StructLayout &layout : layouts) {
        put_u32(out, layout.slot_count);
        put_u32(out, layout.copy_by_value ? 1u : 0u);
        put_u32(out, static_cast<u32>(layout.nested.size()));
        for (const auto &entry : layout.nested) {
            put_u32(out, entry.first);
            put_u32(out, entry.second);
        }
    }

    put_u32(out, static_cast<u32>(functions.size()));
    for (const BytecodeFunction &fn : functions) {
        put_string(out, fn.name);
        put_u32(out, fn.entry);
        put_u32(out, fn.instruction_count);
        put_u32(out, fn.param_count);
        put_u32(out, fn.local_count);
        put_u32(out, fn.register_count);
        put_u32(out, fn.frame_size);
        put_u32(out, (fn.returns_value ? 1u : 0u) | (fn.is_async ? 2u : 0u) |
                         (fn.is_extern_native ? 4u : 0u));
        put_string(out, fn.native_symbol);
        put_u32(out, static_cast<u32>(fn.struct_locals.size()));
        for (const auto &entry : fn.struct_locals) {
            put_u32(out, entry.first);
            put_u32(out, entry.second);
        }
    }

    put_u32(out, static_cast<u32>(code.size()));
    for (const Instr &instruction : code) {
        out.push_back(static_cast<char>(instruction.op));
        put_u32(out, instruction.dest);
        put_u32(out, instruction.a);
        put_u32(out, instruction.b);
        put_u32(out, instruction.c);
        put_u64(out, static_cast<u64>(instruction.imm));
        u64 bits = 0;
        std::memcpy(&bits, &instruction.fimm, sizeof(bits));
        put_u64(out, bits);
        put_u32(out, instruction.arg_offset);
        put_u32(out, instruction.arg_count);
        put_u32(out, instruction.line);
    }
    return out;
}

bool BytecodeProgram::deserialize(const std::string &bytes, BytecodeProgram &out,
                                  std::string &error) {
    if (bytes.size() < sizeof(kMagic) ||
        std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        error = "not a ppc bytecode image, or built by an incompatible version";
        return false;
    }

    Reader reader{bytes, sizeof(kMagic), true};
    out.source_name = reader.string_value();
    out.entry = reader.u32_value();

    const u32 string_count = reader.u32_value();
    out.strings.clear();
    for (u32 i = 0; i < string_count && reader.ok; ++i) {
        out.strings.push_back(reader.string_value());
    }

    const u32 argument_count = reader.u32_value();
    out.arguments.clear();
    for (u32 i = 0; i < argument_count && reader.ok; ++i) {
        out.arguments.push_back(reader.u32_value());
    }

    const u32 layout_count = reader.u32_value();
    out.layouts.clear();
    for (u32 i = 0; i < layout_count && reader.ok; ++i) {
        StructLayout layout;
        layout.slot_count = reader.u32_value();
        layout.copy_by_value = reader.u32_value() != 0;
        const u32 nested_count = reader.u32_value();
        for (u32 k = 0; k < nested_count && reader.ok; ++k) {
            const u32 field = reader.u32_value();
            const u32 decl = reader.u32_value();
            layout.nested.emplace_back(field, decl);
        }
        out.layouts.push_back(std::move(layout));
    }

    const u32 function_count = reader.u32_value();
    out.functions.clear();
    for (u32 i = 0; i < function_count && reader.ok; ++i) {
        BytecodeFunction fn;
        fn.name = reader.string_value();
        fn.entry = reader.u32_value();
        fn.instruction_count = reader.u32_value();
        fn.param_count = reader.u32_value();
        fn.local_count = reader.u32_value();
        fn.register_count = reader.u32_value();
        fn.frame_size = reader.u32_value();
        const u32 flags = reader.u32_value();
        fn.returns_value = (flags & 1u) != 0;
        fn.is_async = (flags & 2u) != 0;
        fn.is_extern_native = (flags & 4u) != 0;
        fn.native_symbol = reader.string_value();
        const u32 struct_local_count = reader.u32_value();
        for (u32 k = 0; k < struct_local_count && reader.ok; ++k) {
            const u32 slot = reader.u32_value();
            const u32 decl = reader.u32_value();
            fn.struct_locals.emplace_back(slot, decl);
        }
        out.functions.push_back(std::move(fn));
    }

    const u32 instruction_count = reader.u32_value();
    out.code.clear();
    for (u32 i = 0; i < instruction_count && reader.ok; ++i) {
        if (!reader.need(1)) break;
        Instr instruction;
        instruction.op = static_cast<Op>(static_cast<unsigned char>(bytes[reader.offset++]));
        instruction.dest = reader.u32_value();
        instruction.a = reader.u32_value();
        instruction.b = reader.u32_value();
        instruction.c = reader.u32_value();
        instruction.imm = static_cast<i64>(reader.u64_value());
        const u64 bits = reader.u64_value();
        std::memcpy(&instruction.fimm, &bits, sizeof(instruction.fimm));
        instruction.arg_offset = reader.u32_value();
        instruction.arg_count = reader.u32_value();
        instruction.line = reader.u32_value();
        out.code.push_back(instruction);
    }

    if (!reader.ok) {
        error = "bytecode image is truncated or corrupt";
        return false;
    }
    return true;
}

}  // namespace ppc
