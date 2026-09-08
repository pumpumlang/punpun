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
};

struct Result {
    pphir::Program hir;
    ppmir::Program mir;
    std::unordered_map<std::string, FunctionFingerprint> functions;
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

inline Result build(const std::vector<Module> &modules, bool optimize) {
    Result result;
    result.hir = pphir::Lowerer(modules).lower();
    if (optimize) pphir::optimize(result.hir);
    pphir::Lowerer::verify(result.hir);
    result.mir = ppmir::Lowerer(result.hir).lower();
    ppmir::Lowerer::verify(result.mir);

    std::unordered_map<std::string, const pphir::Function *> hir_functions;
    for (const auto &function : result.hir.functions) hir_functions[function.name] = &function;
    std::unordered_set<std::string> seen;
    std::uint64_t program = UINT64_C(14695981039346656037);
    for (const auto &function : result.mir.functions) {
        auto found = hir_functions.find(function.name);
        if (found == hir_functions.end())
            throw Error("internal compiler error: MIR function has no typed HIR authority: " + function.name);
        if (!seen.insert(function.name).second)
            throw Error("internal compiler error: duplicate function in authoritative MIR: " + function.name);
        const std::string iface = interface_text(*found->second);
        const std::string body = mir_body_text(function);
        std::uint64_t ih = hash_bytes(UINT64_C(14695981039346656037), iface);
        std::uint64_t bh = hash_bytes(UINT64_C(14695981039346656037), body);
        result.functions[function.name] = {hex(ih), hex(bh)};
        program = hash_bytes(program, function.name + ':' + hex(ih) + ':' + hex(bh) + '\n');
    }
    if (seen.size() != result.hir.functions.size())
        throw Error("internal compiler error: HIR/MIR function-set mismatch");
    result.program_hash = hex(program);
    return result;
}

} // namespace pppipeline
#endif
