#ifndef PUNPUN_PIPELINE_HPP
#define PUNPUN_PIPELINE_HPP

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "hir.hpp"
#include "hir_opt.hpp"
#include "mir.hpp"
#include "machine_ir.hpp"

namespace pppipeline {

inline std::uint64_t hash_bytes(std::uint64_t hash, const std::string &value) {
    for (unsigned char c : value) { hash ^= c; hash *= UINT64_C(1099511628211); }
    return hash;
}
inline std::string hex(std::uint64_t value) {
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << value; return out.str();
}

struct FunctionFingerprint {
    std::string interface_hash;
    std::string body_hash;
    // Hash only the ABI/layout facts this function actually depends on. This
    // is intentionally narrower than Result::abi_hash so unrelated API or
    // shape edits do not evict every native function object.
    std::string dependency_hash;
};

struct Result {
    pphir::Program hir;
    ppmir::Program mir;
    ppmachine::Program machine;
    std::unordered_map<std::string, FunctionFingerprint> functions;
    // Hash of all source-visible ABI/layout facts. Native per-function object
    // cache keys include this so a shape layout or callable interface change
    // can never reuse an object compiled against stale offsets/call blocks.
    std::string abi_hash;
    std::string program_hash;
};

inline std::string interface_text(const pphir::Function &function) {
    std::ostringstream out;
    out << function.name << '|' << type_name(function.result) << '|'
        << function.external_native << '|' << function.is_async << '|';
    for (const auto &[name, type] : function.parameters) out << name << ':' << type_name(type) << ';';
    return out.str();
}

inline std::string mir_body_text(const ppmir::Function &function) {
    std::ostringstream out;
    out << function.name << '|' << type_name(function.result) << '|' << function.spill_slots << '|';
    for (const auto &block : function.blocks) {
        out << "bb" << block.id << '{';
        for (const auto &instruction : block.instructions) {
            out << static_cast<int>(instruction.op) << ':' << instruction.result << ':'
                << type_name(instruction.type) << ':' << instruction.detail << ':';
            for (auto operand : instruction.operands) out << operand << ',';
            out << ';';
        }
        out << "t" << static_cast<int>(block.terminator.kind) << ':' << block.terminator.value
            << ':' << block.terminator.first << ':' << block.terminator.second << '}';
    }
    return out.str();
}


inline std::string machine_body_text(const ppmachine::Function &function) {
    std::ostringstream out;
    out << function.name << '|' << type_name(function.result) << '|'
        << static_cast<int>(function.abi.convention) << '|'
        << function.abi.argument_block_size << '|'
        << function.abi.hidden_result_pointer << '|'
        << function.stack_slots << '|';
    for (const auto &parameter : function.abi.parameters)
        out << type_name(parameter.type) << ':' << static_cast<int>(parameter.location.kind) << ':'
            << parameter.location.name << ':' << parameter.location.offset << ':' << parameter.location.size << ';';
    out << "ret:" << static_cast<int>(function.abi.result.location.kind) << ':'
        << function.abi.result.location.name << ':' << function.abi.result.location.offset << ':'
        << function.abi.result.location.size << '|';
    for (const auto &block : function.blocks) {
        out << "bb" << block.id << '{';
        for (const auto &instruction : block.instructions) {
            out << static_cast<int>(instruction.op) << ':' << instruction.result << ':'
                << type_name(instruction.type) << ':' << instruction.detail << ':';
            for (auto operand : instruction.operands) out << operand << ',';
            out << "tok=" << instruction.token.file.string() << ':' << instruction.token.line << ':'
                << instruction.token.column << ':' << instruction.token.length << ':';
            if (instruction.call.known_function) {
                out << "call=" << static_cast<int>(instruction.call.convention) << ':'
                    << instruction.call.hidden_result_pointer << ':' << instruction.call.argument_block_size << ':';
                for (const auto &argument : instruction.call.arguments)
                    out << type_name(argument.type) << '@' << static_cast<int>(argument.location.kind) << ':'
                        << argument.location.offset << ':' << argument.location.size << ',';
                out << "ret=" << type_name(instruction.call.result.type) << '@'
                    << static_cast<int>(instruction.call.result.location.kind) << ':'
                    << instruction.call.result.location.offset << ':' << instruction.call.result.location.size << ':';
            }
            if (instruction.result != ppmachine::NoValue) {
                const auto &location = function.locations.at(instruction.result);
                out << '@' << static_cast<int>(location.kind) << ':' << location.name << ':'
                    << location.stack_slot << ':' << location.stack_slots;
            }
            out << ';';
        }
        out << "t" << static_cast<int>(block.terminator.kind) << ':' << block.terminator.value
            << ':' << block.terminator.first << ':' << block.terminator.second << '}';
    }
    return out.str();
}


inline std::string shape_dependency_text(const Shape &shape) {
    std::ostringstream out;
    out << shape.name << "|ref=" << shape.reference_type << "|init=" << shape.initializer_name << '|';
    for (const Parameter &field : shape.fields)
        out << field.name << ':' << type_name(field.type) << ':' << static_cast<int>(field.visibility) << ';';
    for (const EnumVariant &variant : shape.enum_variants) {
        out << "variant:" << variant.name << '(';
        for (const Type &type : variant.payload) out << type_name(type) << ',';
        out << ");";
    }
    return out.str();
}

inline void collect_type_dependencies(const Type &type,
                                      const std::unordered_map<std::string, const Shape *> &shapes,
                                      std::unordered_set<std::string> &seen,
                                      std::vector<std::string> &entries) {
    Type current = type;
    if (is_pointer_like_type(current)) current = pointee_type(current);
    const auto found = shapes.find(current.name);
    if (found == shapes.end() || !seen.insert(current.name).second) return;
    const Shape &shape = *found->second;
    entries.push_back("shape:" + shape_dependency_text(shape));
    for (const Parameter &field : shape.fields)
        collect_type_dependencies(field.type, shapes, seen, entries);
    for (const EnumVariant &variant : shape.enum_variants)
        for (const Type &payload : variant.payload)
            collect_type_dependencies(payload, shapes, seen, entries);
}

inline std::string function_dependency_text(
        const ppmachine::Function &function,
        const std::unordered_map<std::string, const Shape *> &shapes,
        const std::unordered_map<std::string, std::string> &interface_hashes) {
    std::vector<std::string> entries;
    std::unordered_set<std::string> seen_shapes;
    std::unordered_set<std::string> seen_calls;
    collect_type_dependencies(function.result, shapes, seen_shapes, entries);
    for (const auto &[name, type] : function.parameters) {
        (void)name;
        collect_type_dependencies(type, shapes, seen_shapes, entries);
    }
    for (const ppmachine::Block &block : function.blocks) {
        for (const ppmachine::Instruction &instruction : block.instructions) {
            collect_type_dependencies(instruction.type, shapes, seen_shapes, entries);
            for (const ppmachine::AbiValue &argument : instruction.call.arguments)
                collect_type_dependencies(argument.type, shapes, seen_shapes, entries);
            collect_type_dependencies(instruction.call.result.type, shapes, seen_shapes, entries);
            if (instruction.call.known_function && seen_calls.insert(instruction.detail).second) {
                const auto target = interface_hashes.find(instruction.detail);
                if (target != interface_hashes.end())
                    entries.push_back("call:" + instruction.detail + ':' + target->second);
            }
            if (instruction.op == pphir::Op::Construct) {
                const auto shape = shapes.find(instruction.type.name);
                if (shape != shapes.end() && !shape->second->initializer_name.empty()) {
                    const std::string &initializer = shape->second->initializer_name;
                    if (seen_calls.insert(initializer).second) {
                        const auto target = interface_hashes.find(initializer);
                        if (target != interface_hashes.end())
                            entries.push_back("call:" + initializer + ':' + target->second);
                    }
                }
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream out;
    for (const std::string &entry : entries) out << entry << '\n';
    return out.str();
}

inline std::string abi_text(const std::vector<Module> &modules, const pphir::Program &hir) {
    std::vector<std::string> entries;
    for (const Module &module : modules) {
        for (const Shape &shape : module.shapes) {
            std::ostringstream out;
            out << "shape:" << shape.name << ":ref=" << shape.reference_type << ':';
            for (const Parameter &field : shape.fields)
                out << field.name << ':' << type_name(field.type) << ':' << static_cast<int>(field.visibility) << ';';
            for (const EnumVariant &variant : shape.enum_variants) {
                out << "variant:" << variant.name << '(';
                for (const Type &type : variant.payload) out << type_name(type) << ',';
                out << ");";
            }
            entries.push_back(out.str());
        }
    }
    for (const pphir::Function &function : hir.functions)
        entries.push_back("fn:" + interface_text(function));
    std::sort(entries.begin(), entries.end());
    std::ostringstream out;
    for (const std::string &entry : entries) out << entry << '\n';
    return out.str();
}

inline Result build(const std::vector<Module> &modules, bool optimize) {
    Result result;
    result.hir = pphir::Lowerer(modules).lower();
    if (optimize) pphir::optimize(result.hir);
    pphir::Lowerer::verify(result.hir);
    result.mir = ppmir::Lowerer(result.hir).lower();
    ppmir::Lowerer::verify(result.mir);
    result.machine = ppmachine::Lowerer(result.mir, modules, ppmachine::Target::X86_64SysV, optimize).lower();
    ppmachine::Lowerer::verify(result.machine);
    result.abi_hash = hex(hash_bytes(UINT64_C(14695981039346656037), abi_text(modules, result.hir)));

    std::unordered_map<std::string, const pphir::Function *> hir_functions;
    std::unordered_map<std::string, std::string> interface_hashes;
    for (const auto &function : result.hir.functions) {
        hir_functions[function.name] = &function;
        const std::string iface = interface_text(function);
        interface_hashes[function.name] = hex(hash_bytes(UINT64_C(14695981039346656037), iface));
    }
    std::unordered_map<std::string, const Shape *> shapes;
    for (const Module &module : modules)
        for (const Shape &shape : module.shapes) shapes[shape.name] = &shape;

    std::unordered_set<std::string> seen;
    std::uint64_t program = UINT64_C(14695981039346656037);
    program = hash_bytes(program, "abi:" + result.abi_hash + "\n");
    for (const auto &function : result.machine.functions) {
        auto found = hir_functions.find(function.name);
        if (found == hir_functions.end())
            throw Error("internal compiler error: Machine IR function has no typed HIR authority: " + function.name);
        if (!seen.insert(function.name).second)
            throw Error("internal compiler error: duplicate function in authoritative Machine IR: " + function.name);
        const std::string body = machine_body_text(function);
        const std::string dependencies = function_dependency_text(function, shapes, interface_hashes);
        const std::string ih = interface_hashes.at(function.name);
        const std::string bh = hex(hash_bytes(UINT64_C(14695981039346656037), body));
        const std::string dh = hex(hash_bytes(UINT64_C(14695981039346656037), dependencies));
        result.functions[function.name] = {ih, bh, dh};
        program = hash_bytes(program, function.name + ':' + ih + ':' + bh + ':' + dh + '\n');
    }
    if (seen.size() != result.hir.functions.size())
        throw Error("internal compiler error: HIR/Machine-IR function-set mismatch");
    result.program_hash = hex(program);
    return result;
}

} // namespace pppipeline
#endif
