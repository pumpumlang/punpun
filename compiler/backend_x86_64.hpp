// PunPun direct x86-64 backend.
//
// Emits GNU-assembler (Intel syntax) for System V AMD64 Linux and links against
// the same native runtime used by the portable and LLVM paths. Parsing, semantic
// analysis, ownership checks, typed HIR, verified MIR and Machine IR have already completed.
// Machine IR is authoritative for function scheduling, ABI layout and allocation
// metadata and every direct-native function body. Step 7 removes the prior
// typed-source body emitter: direct native instruction selection consumes only
// verified Machine IR operations and allocation metadata.
//
// Conventions
// -----------
//   * Scalars (int/bool/str/nums, and float as its 64-bit pattern) are produced
//     in RAX. A record expression produces the ADDRESS of its bytes in RAX.
//   * Scalar virtual registers use Machine-IR physical register homes when safe.
//     Call-live and CFG-crossing values obey conservative allocation constraints;
//     stack spills use allocator-owned ranges and verified 16-byte frame alignment.
//   * Runtime helpers use the ordinary SysV ABI. PunPun-to-PunPun calls use a
//     private convention: RDI holds a pointer to a caller-built argument block;
//     record results are written through a hidden destination pointer stored at
//     block offset 0. This keeps by-value records correct without implementing
//     SysV aggregate classification.
#ifndef PUNPUN_BACKEND_X86_64_HPP
#define PUNPUN_BACKEND_X86_64_HPP

#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend.hpp"
#include "builtins.hpp"
#include "ownership.hpp"
#include "machine_ir.hpp"

class X86Backend {
  public:
    explicit X86Backend(const std::vector<Module> &modules, const ppmachine::Program &machine) : modules_(modules), machine_(machine) {
        for (const BuiltinSpec &builtin : punpun_builtins()) is_builtin_.insert(builtin.name);
        for (const ppmachine::Function &function : machine_.functions) machine_functions_[function.name] = &function;
        for (const auto &module : modules_)
            for (const auto &shape : module.shapes) shapes_[shape.name] = &shape;
        for (const auto &module : modules_)
            for (const auto &function : module.functions) {
                Signature signature;
                signature.result = function.result;
                signature.is_async = function.is_async;
                for (const auto &parameter : function.parameters)
                    signature.parameters.push_back(parameter.type);
                user_signatures_[function.name] = std::move(signature);
                if (function.is_extern_native) native_symbols_[function.name] = function.native_symbol;
            }
    }

    std::string generate() {
        begin_unit();
        for (const ppmachine::Function &machine_function : machine_.functions) {
            if (machine_function.external_native) continue;
            generate_machine_function(machine_function);
        }
        generate_entry();
        return finish_unit();
    }

    // Step 7.3 object-cache entry points. Each returned translation unit is
    // independently assemblable, which lets the driver cache/reuse one native
    // object per PunPun function instead of recompiling the whole program.
    std::string generate_function_unit(const std::string &name) {
        begin_unit();
        const ppmachine::Function &function = machine_function(name);
        if (function.external_native) internal("cannot emit native object for extern function '" + name + "'");
        generate_machine_function(function);
        return finish_unit();
    }

    std::string generate_entry_unit() {
        begin_unit();
        generate_entry();
        return finish_unit();
    }

  private:
    struct Signature {
        std::vector<Type> parameters;
        Type result;
        bool is_async = false;
    };
    struct FieldInfo {
        int64_t offset;
        Type type;
    };
    struct ShapeLayout {
        int64_t size = 0;
        std::unordered_map<std::string, FieldInfo> fields;
        std::vector<std::string> order;
    };
    struct Var {
        Type type;
        int64_t offset;   // rbp-relative for locals, block-relative for params
        bool is_param;
        int64_t alive_offset = 0; // rbp-relative boolean, 0 when this binding is non-owning
    };

    const std::vector<Module> &modules_;
    const ppmachine::Program &machine_;
    std::unordered_map<std::string, const ppmachine::Function *> machine_functions_;
    std::unordered_set<std::string> is_builtin_;
    std::unordered_map<std::string, const Shape *> shapes_;
    std::unordered_map<std::string, ShapeLayout> layouts_;
    std::unordered_map<std::string, Signature> user_signatures_;
    std::unordered_map<std::string, std::string> native_symbols_;
    std::unordered_map<std::string, int> debug_files_;

    std::ostringstream text_;
    std::ostringstream rodata_;
    std::ostringstream body_;
    std::vector<std::unordered_map<std::string, Var>> scopes_;
    std::vector<std::vector<Var>> drop_scopes_;
    struct LoopInfo { std::string break_label; std::string continue_label; std::size_t scope_base; };
    std::vector<LoopInfo> loops_;
    Type current_result_ = Type::Void;
    bool result_is_record_ = false;
    std::string ret_label_;
    int64_t args_ptr_off_ = 0;
    int64_t frame_ = 0;
    size_t label_count_ = 0;
    size_t string_count_ = 0;

    [[noreturn]] static void internal(const std::string &message) {
        throw Error("internal error: " + message);
    }

    void begin_unit() {
        text_.str(""); text_.clear();
        rodata_.str(""); rodata_.clear();
        body_.str(""); body_.clear();
        debug_files_.clear();
        label_count_ = 0;
        string_count_ = 0;
        text_ << ".intel_syntax noprefix\n";
        int debug_id = 1;
        for (const auto &module : modules_) {
            const std::string key = module.file.lexically_normal().string();
            if (debug_files_.count(key)) continue;
            debug_files_[key] = debug_id;
            text_ << ".file " << debug_id++ << " \"" << asm_escaped(key) << "\"\n";
        }
        text_ << ".text\n";
    }

    std::string finish_unit() const {
        std::ostringstream out;
        out << text_.str();
        out << ".section .rodata\n" << rodata_.str();
        out << ".section .note.GNU-stack,\"\",@progbits\n";
        return out.str();
    }

    // -- small helpers ------------------------------------------------------
    static int64_t align_up(int64_t value, int64_t to) {
        return (value + to - 1) / to * to;
    }
    int64_t alloc(int64_t bytes) {
        frame_ += align_up(bytes, 8);
        return -frame_;
    }
    static std::string mem_rbp(int64_t offset) {
        if (offset >= 0) return "[rbp + " + std::to_string(offset) + "]";
        return "[rbp - " + std::to_string(-offset) + "]";
    }
    static std::string mem(const std::string &reg, int64_t offset) {
        if (offset >= 0) return "[" + reg + " + " + std::to_string(offset) + "]";
        return "[" + reg + " - " + std::to_string(-offset) + "]";
    }
    void emit(const std::string &instruction) { body_ << "    " << instruction << "\n"; }
    void label(const std::string &name) { body_ << name << ":\n"; }
    std::string new_label() { return ".L" + std::to_string(label_count_++); }
    static std::string asm_escaped(const std::string &value) {
        std::string result;
        for (char c : value) {
            if (c == '\\' || c == '"') result += '\\';
            result += c;
        }
        return result;
    }
    void debug_location(const Token &token) {
        const auto found = debug_files_.find(token.file.lexically_normal().string());
        if (found != debug_files_.end())
            body_ << "    .loc " << found->second << " " << token.line << " " << token.column << "\n";
    }
    static std::string hex_u64(uint64_t value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(value));
        return buffer;
    }

    bool is_object(const Type &type) const {
        auto found = shapes_.find(type.name);
        return found != shapes_.end() && found->second->reference_type;
    }
    bool is_record(const Type &type) const {
        auto found = shapes_.find(type.name);
        return found != shapes_.end() && !found->second->reference_type;
    }
    static std::string function_symbol(const std::string &name) {
        std::string result = "pp_fn_";
        result.reserve(name.size() + 6);
        for (unsigned char c : name) {
            if (std::isalnum(c) || c == '_') result += static_cast<char>(c);
            else {
                static const char hex[] = "0123456789abcdef";
                result += '_'; result += hex[c >> 4]; result += hex[c & 15];
            }
        }
        return result;
    }

    const ppmachine::Function &machine_function(const std::string &name) const {
        const auto found = machine_functions_.find(name);
        if (found == machine_functions_.end())
            internal("Machine IR has no function '" + name + "'");
        return *found->second;
    }

    const ShapeLayout &layout(const std::string &name) {
        auto found = layouts_.find(name);
        if (found != layouts_.end()) return found->second;
        const Shape *shape = shapes_.at(name);
        ShapeLayout result;
        int64_t offset = 0;
        for (const auto &field : shape->fields) {
            result.fields[field.name] = {offset, field.type};
            result.order.push_back(field.name);
            offset += type_size(field.type);
        }
        result.size = offset == 0 ? 8 : offset;
        return layouts_.emplace(name, std::move(result)).first->second;
    }
    int64_t type_size(const Type &type) {
        return is_record(type) ? layout(type.name).size : 8;
    }

    std::string add_string(const std::string &value) {
        const std::string name = ".Lstr" + std::to_string(string_count_++);
        rodata_ << name << ":\n    .byte ";
        for (unsigned char c : value) rodata_ << static_cast<int>(c) << ", ";
        rodata_ << "0\n";
        return name;
    }

    // Copy `size` bytes with RDI=dest, RSI=src, using RAX as scratch.
    void copy_via_rdi_rsi(int64_t size) {
        for (int64_t k = 0; k < size; k += 8) {
            emit("mov rax, " + mem("rsi", k));
            emit("mov " + mem("rdi", k) + ", rax");
        }
    }
    // Copy `size` bytes from RSI into a frame slot at `dst_off`, RAX scratch.
    void copy_rsi_to_frame(int64_t dst_off, int64_t size) {
        for (int64_t k = 0; k < size; k += 8) {
            emit("mov rax, " + mem("rsi", k));
            emit("mov " + mem_rbp(dst_off + k) + ", rax");
        }
    }

    const Var *lookup(const std::string &name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    bool needs_drop(const Type &type) const { return ppownership::type_needs_drop(type, shapes_); }

    void load_var_address(const Var &variable) {
        if (variable.is_param) {
            emit("mov rcx, " + mem_rbp(args_ptr_off_));
            emit("lea rax, " + mem("rcx", variable.offset));
        } else emit("lea rax, " + mem_rbp(variable.offset));
    }

    // RAX enters as the address of storage holding `type`.
    void emit_drop_at_address(const Type &type) {
        if (!needs_drop(type)) return;
        const int64_t address = alloc(8);
        emit("mov " + mem_rbp(address) + ", rax");
        if (type == Type::Nums) {
            emit("mov rcx, " + mem_rbp(address));
            emit("mov rdi, " + mem("rcx", 0));
            emit("call pp_numbers_free@PLT");
            return;
        }
        auto found = shapes_.find(type.name);
        if (found == shapes_.end()) return;
        const Shape &shape = *found->second;
        if (shape.reference_type) {
            const int64_t object_ptr = alloc(8);
            emit("mov rcx, " + mem_rbp(address));
            emit("mov rax, " + mem("rcx", 0));
            emit("mov " + mem_rbp(object_ptr) + ", rax");
            const std::string done = new_label();
            emit("test rax, rax");
            emit("je " + done);
            const ShapeLayout &shape_layout = layout(shape.name);
            for (auto field = shape.fields.rbegin(); field != shape.fields.rend(); ++field) {
                if (!needs_drop(field->type)) continue;
                emit("mov rax, " + mem_rbp(object_ptr));
                const int64_t offset = shape_layout.fields.at(field->name).offset;
                if (offset) emit("add rax, " + std::to_string(offset));
                emit_drop_at_address(field->type);
            }
            emit("mov rdi, " + mem_rbp(object_ptr));
            emit("call pp_object_free@PLT");
            label(done);
            return;
        }
        const ShapeLayout &shape_layout = layout(shape.name);
        for (auto field = shape.fields.rbegin(); field != shape.fields.rend(); ++field) {
            if (!needs_drop(field->type)) continue;
            emit("mov rax, " + mem_rbp(address));
            const int64_t offset = shape_layout.fields.at(field->name).offset;
            if (offset) emit("add rax, " + std::to_string(offset));
            emit_drop_at_address(field->type);
        }
    }

    void emit_drop_var(const Var &variable) {
        if (variable.alive_offset == 0) return;
        const std::string skip = new_label();
        emit("cmp qword ptr " + mem_rbp(variable.alive_offset) + ", 0");
        emit("je " + skip);
        load_var_address(variable);
        emit_drop_at_address(variable.type);
        emit("mov qword ptr " + mem_rbp(variable.alive_offset) + ", 0");
        label(skip);
    }

    void emit_scope_drops(std::size_t depth) {
        if (depth >= drop_scopes_.size()) return;
        auto &values = drop_scopes_[depth];
        for (auto it = values.rbegin(); it != values.rend(); ++it) emit_drop_var(*it);
    }

    void emit_drops_from(std::size_t first_depth) {
        for (std::size_t depth = drop_scopes_.size(); depth-- > first_depth;) emit_scope_drops(depth);
    }

    void register_drop(Var &variable) {
        if (!needs_drop(variable.type)) return;
        variable.alive_offset = alloc(8);
        emit("mov qword ptr " + mem_rbp(variable.alive_offset) + ", 1");
        drop_scopes_.back().push_back(variable);
    }

    // -- Machine-IR body emission -------------------------------------------
    // Step 7.2 complete: the direct x86 backend consumes verified Machine IR
    // for every PunPun function body. The typed source AST remains available to
    // diagnostics/C lowering, but native instruction selection no longer walks
    // statements or expressions.
    struct MachineLocalHome {
        Type type = Type::Void;
        int64_t offset = 0;
        int64_t alive_offset = 0;
    };

    struct MachineFramePlan {
        int64_t frame = 0;
        int64_t args_pointer = 0;
        int64_t call_area = 0;
        std::size_t call_area_size = 0;
        // Byte range backing Machine-IR stack allocations. Register-allocated
        // scalars do not receive redundant frame homes in Step 7.4.
        int64_t machine_stack_base = 0;
        int64_t scratch_area = 0;
        int64_t spill_area = 0;
        std::unordered_map<ppmachine::VReg, int64_t> values;
        std::unordered_map<ppmachine::VReg, std::string> value_registers;
        std::unordered_map<ppmachine::VReg, int64_t> aggregate_values;
        std::unordered_map<std::string, MachineLocalHome> locals;
        std::unordered_map<ppmachine::VReg, Type> types;
        std::unordered_map<ppmachine::VReg, std::size_t> parameter_indices;
        std::unordered_map<ppmachine::VReg, std::string> integer_constants;
    };

    static int64_t plan_alloc(MachineFramePlan &plan, std::size_t bytes) {
        const std::size_t aligned = (bytes + 7) / 8 * 8;
        plan.frame += static_cast<int64_t>(aligned);
        return -plan.frame;
    }

    Type machine_value_type(const MachineFramePlan &plan, ppmachine::VReg value) const {
        const auto found = plan.types.find(value);
        if (found == plan.types.end()) internal("Machine IR value has no type");
        return found->second;
    }

    static std::string storage_from_address_detail(const std::string &detail) {
        const std::size_t space = detail.find(' ');
        if (space == std::string::npos || space + 1 >= detail.size()) return {};
        return detail.substr(space + 1);
    }

    std::size_t required_call_area(const ppmachine::Instruction &instruction) const {
        if (instruction.op == pphir::Op::Call && instruction.call.known_function &&
            instruction.call.convention == ppmachine::CallingConvention::PunPunBlock)
            return instruction.call.argument_block_size;
        if (instruction.op == pphir::Op::Construct) {
            auto shape_it = shapes_.find(instruction.type.name);
            if (shape_it != shapes_.end() && shape_it->second->reference_type &&
                !shape_it->second->initializer_name.empty())
                return machine_function(shape_it->second->initializer_name).abi.argument_block_size;
        }
        return 0;
    }

    MachineFramePlan plan_machine_frame(const ppmachine::Function &function) {
        MachineFramePlan plan;
        plan.args_pointer = plan_alloc(plan, 8);

        // Reserve the exact allocator-owned spill/aggregate range once. Values
        // assigned to registers stay in those registers; stack-assigned values
        // share/reuse slots exactly as decided by Machine IR.
        if (function.stack_frame_bytes != 0)
            plan.machine_stack_base = plan_alloc(plan, function.stack_frame_bytes);

        // Types are function-global and block creation order is not dominance
        // order, so discover all virtual-register types first.
        for (const auto &block : function.blocks)
            for (const auto &instruction : block.instructions)
                if (instruction.result != ppmachine::NoValue)
                    plan.types[instruction.result] = instruction.type;

        std::size_t parameter_index = 0;
        for (const auto &block : function.blocks) {
            for (const auto &instruction : block.instructions) {
                if (instruction.result != ppmachine::NoValue) {
                    const auto location_it = function.locations.find(instruction.result);
                    if (location_it == function.locations.end())
                        internal("Machine IR value has no physical allocation");
                    const ppmachine::Location &location = location_it->second;
                    if (location.kind == ppmachine::Location::Kind::Register) {
                        if (is_record(instruction.type))
                            internal("aggregate Machine IR value assigned to a register");
                        plan.value_registers[instruction.result] = location.name;
                    } else {
                        if (function.stack_frame_bytes == 0)
                            internal("Machine IR stack value exists without a spill frame");
                        const int64_t home = plan.machine_stack_base + static_cast<int64_t>(location.stack_slot * 8);
                        plan.values[instruction.result] = home;
                        if (is_record(instruction.type)) plan.aggregate_values[instruction.result] = home;
                    }
                    if (instruction.op == pphir::Op::Parameter)
                        plan.parameter_indices[instruction.result] = parameter_index++;
                    if (instruction.op == pphir::Op::Constant && instruction.type == Type::Int)
                        plan.integer_constants[instruction.result] = instruction.detail;
                }
                if (instruction.op == pphir::Op::Store && !plan.locals.count(instruction.detail)) {
                    if (instruction.operands.empty()) internal("Machine IR store has no value");
                    const Type type = machine_value_type(plan, instruction.operands.front());
                    MachineLocalHome home;
                    home.type = type;
                    home.offset = plan_alloc(plan, static_cast<std::size_t>(type_size(type)));
                    if (needs_drop(type)) home.alive_offset = plan_alloc(plan, 8);
                    plan.locals.emplace(instruction.detail, home);
                }
                plan.call_area_size = std::max(plan.call_area_size, required_call_area(instruction));
            }
        }
        if (plan.call_area_size != 0) plan.call_area = plan_alloc(plan, plan.call_area_size);
        // Drop recursion and ABI shuffles use fixed spill scratch. This is
        // deliberately separate from Machine-IR allocation so helper calls
        // never overwrite a live virtual register or reusable spill slot.
        plan.scratch_area = plan_alloc(plan, 256);
        return plan;
    }

    static bool machine_xmm(const std::string &name) { return name.rfind("xmm", 0) == 0; }

    void machine_load(const MachineFramePlan &plan, ppmachine::VReg value, const std::string &reg) {
        const Type type = machine_value_type(plan, value);
        if (is_record(type)) {
            const auto found = plan.values.find(value);
            if (found == plan.values.end()) internal("aggregate Machine IR value has no spill home");
            emit("lea " + reg + ", " + mem_rbp(found->second));
            return;
        }
        if (const auto assigned = plan.value_registers.find(value); assigned != plan.value_registers.end()) {
            const std::string &home = assigned->second;
            if (home == reg) return;
            if (machine_xmm(home)) emit("movq " + reg + ", " + home);
            else emit("mov " + reg + ", " + home);
            return;
        }
        const auto found = plan.values.find(value);
        if (found == plan.values.end()) internal("Machine IR value has no allocated home");
        emit("mov " + reg + ", " + mem_rbp(found->second));
    }

    void machine_store(const MachineFramePlan &plan, ppmachine::VReg value, const std::string &reg = "rax") {
        const Type type = machine_value_type(plan, value);
        if (is_record(type)) {
            const auto found = plan.values.find(value);
            if (found == plan.values.end()) internal("aggregate Machine IR result has no spill home");
            // Record virtual registers are allocated as byte ranges. A record
            // producer yields its source address in a GPR; materialize the
            // by-value result into the allocator-owned range.
            if (reg != "rsi") emit("mov rsi, " + reg);
            machine_copy_pointer_to_frame(found->second, type, "rsi");
            return;
        }
        if (const auto assigned = plan.value_registers.find(value); assigned != plan.value_registers.end()) {
            const std::string &home = assigned->second;
            if (machine_xmm(home)) emit("movq " + home + ", " + reg);
            else if (home != reg) emit("mov " + home + ", " + reg);
            return;
        }
        const auto found = plan.values.find(value);
        if (found == plan.values.end()) internal("Machine IR value has no allocated home");
        emit("mov " + mem_rbp(found->second) + ", " + reg);
    }

    int64_t machine_aggregate_storage(const MachineFramePlan &plan, ppmachine::VReg value) const {
        const auto found = plan.aggregate_values.find(value);
        if (found == plan.aggregate_values.end()) internal("aggregate Machine IR value has no byte storage");
        return found->second;
    }

    void machine_copy_pointer_to_frame(int64_t destination, const Type &type, const std::string &source_reg = "rsi") {
        const int64_t size = type_size(type);
        for (int64_t k = 0; k < size; k += 8) {
            emit("mov rax, " + mem(source_reg, k));
            emit("mov " + mem_rbp(destination + k) + ", rax");
        }
    }

    void machine_copy_frame_to_pointer(const std::string &destination_reg, int64_t source,
                                       const Type &type) {
        const int64_t size = type_size(type);
        for (int64_t k = 0; k < size; k += 8) {
            emit("mov rax, " + mem_rbp(source + k));
            emit("mov " + mem(destination_reg, k) + ", rax");
        }
    }

    void machine_copy_value_to_frame(const MachineFramePlan &plan, ppmachine::VReg value,
                                     const Type &type, int64_t destination) {
        if (is_record(type)) {
            machine_load(plan, value, "rsi");
            machine_copy_pointer_to_frame(destination, type, "rsi");
        } else {
            machine_load(plan, value, "rax");
            emit("mov " + mem_rbp(destination) + ", rax");
        }
    }

    void machine_set_result_address(const MachineFramePlan &plan, ppmachine::VReg result,
                                    int64_t storage) {
        if (machine_aggregate_storage(plan, result) != storage)
            internal("aggregate result storage diverged from Machine IR allocation");
    }

    int64_t machine_scratch(const MachineFramePlan &plan, std::size_t depth) const {
        if (depth >= 32) internal("owning type nesting exceeds Machine IR drop scratch capacity");
        return plan.scratch_area + static_cast<int64_t>(depth * 8);
    }

    // RAX enters as the address of storage containing `type`.
    void machine_drop_at_address(const Type &type, const MachineFramePlan &plan, std::size_t depth = 0) {
        if (!needs_drop(type)) return;
        const int64_t saved = machine_scratch(plan, depth);
        emit("mov " + mem_rbp(saved) + ", rax");
        if (type == Type::Nums) {
            emit("mov rcx, " + mem_rbp(saved));
            emit("mov rdi, " + mem("rcx", 0));
            emit("call pp_numbers_free@PLT");
            return;
        }
        auto found = shapes_.find(type.name);
        if (found == shapes_.end()) return;
        const Shape &shape = *found->second;
        const ShapeLayout &shape_layout = layout(shape.name);
        if (shape.reference_type) {
            const int64_t object_saved = machine_scratch(plan, depth + 1);
            emit("mov rcx, " + mem_rbp(saved));
            emit("mov rax, " + mem("rcx", 0));
            emit("mov " + mem_rbp(object_saved) + ", rax");
            const std::string done = new_label();
            emit("test rax, rax");
            emit("je " + done);
            for (auto field = shape.fields.rbegin(); field != shape.fields.rend(); ++field) {
                if (!needs_drop(field->type)) continue;
                emit("mov rax, " + mem_rbp(object_saved));
                const int64_t offset = shape_layout.fields.at(field->name).offset;
                if (offset) emit("add rax, " + std::to_string(offset));
                machine_drop_at_address(field->type, plan, depth + 2);
            }
            emit("mov rdi, " + mem_rbp(object_saved));
            emit("call pp_object_free@PLT");
            label(done);
            return;
        }
        for (auto field = shape.fields.rbegin(); field != shape.fields.rend(); ++field) {
            if (!needs_drop(field->type)) continue;
            emit("mov rax, " + mem_rbp(saved));
            const int64_t offset = shape_layout.fields.at(field->name).offset;
            if (offset) emit("add rax, " + std::to_string(offset));
            machine_drop_at_address(field->type, plan, depth + 1);
        }
    }

    void machine_drop_local(const MachineFramePlan &plan, const std::string &name) {
        const auto found = plan.locals.find(name);
        if (found == plan.locals.end()) internal("Machine IR drop references unknown local '" + name + "'");
        const MachineLocalHome &home = found->second;
        if (home.alive_offset == 0) return;
        const std::string skip = new_label();
        emit("cmp qword ptr " + mem_rbp(home.alive_offset) + ", 0");
        emit("je " + skip);
        emit("lea rax, " + mem_rbp(home.offset));
        machine_drop_at_address(home.type, plan);
        emit("mov qword ptr " + mem_rbp(home.alive_offset) + ", 0");
        label(skip);
    }

    void emit_machine_constant(const ppmachine::Instruction &instruction,
                               const MachineFramePlan &plan) {
        if (instruction.type == Type::Int) {
            unsigned long long bits = 0;
            try {
                if (!instruction.detail.empty() && instruction.detail.front() == '-')
                    bits = static_cast<unsigned long long>(std::stoll(instruction.detail));
                else
                    bits = std::stoull(instruction.detail);
            } catch (...) {
                internal("invalid integer constant reached Machine IR x86 lowering");
            }
            emit("movabs rax, " + hex_u64(static_cast<uint64_t>(bits)));
        } else if (instruction.type == Type::Float) {
            const double value = std::stod(instruction.detail);
            uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            emit("movabs rax, " + hex_u64(bits));
        } else if (instruction.type == Type::Bool) {
            if (instruction.detail == "yes") emit("mov eax, 1");
            else emit("xor eax, eax");
        } else if (instruction.type == Type::Str) {
            emit("lea rax, [rip + " + add_string(instruction.detail) + "]");
        } else internal("unsupported Machine IR constant type");
        machine_store(plan, instruction.result);
    }

    void emit_machine_unary(const ppmachine::Instruction &instruction,
                            const MachineFramePlan &plan) {
        machine_load(plan, instruction.operands.at(0), "rax");
        const Type operand = machine_value_type(plan, instruction.operands.at(0));
        if (instruction.detail == "not") emit("xor rax, 1");
        else if (instruction.detail == "~") emit("not rax");
        else if (instruction.detail == "-") {
            if (operand == Type::Int) {
                const auto literal = plan.integer_constants.find(instruction.operands.at(0));
                if (literal == plan.integer_constants.end() || literal->second != "9223372036854775808") {
                    emit("mov rdi, rax");
                    emit("call pp_neg_i64@PLT");
                }
            } else if (operand == Type::Float) {
                emit("movabs rdi, 0x8000000000000000"); emit("xor rax, rdi");
            } else internal("invalid unary minus in Machine IR");
        } else internal("unsupported Machine IR unary operation '" + instruction.detail + "'");
        machine_store(plan, instruction.result);
    }

    void emit_machine_binary(const ppmachine::Instruction &instruction,
                             const MachineFramePlan &plan) {
        const auto left_value = instruction.operands.at(0);
        const auto right_value = instruction.operands.at(1);
        const Type left = machine_value_type(plan, left_value);
        const Type right = machine_value_type(plan, right_value);
        const std::string &op = instruction.detail;
        machine_load(plan, left_value, "rax");
        machine_load(plan, right_value, "rdi");

        if (is_raw_pointer_type(left) && right == Type::Int && (op == "+" || op == "-")) {
            const int64_t stride = type_size(pointee_type(left));
            if (stride != 1) emit("imul rdi, " + std::to_string(stride));
            emit(std::string(op == "+" ? "add" : "sub") + " rax, rdi");
            machine_store(plan, instruction.result); return;
        }
        if (op == "==" || op == "!=") {
            if (left == Type::Str) {
                emit("mov rsi, rdi"); emit("mov rdi, rax"); emit("call pp_str_eq@PLT");
                if (op == "!=") emit("xor rax, 1");
            } else if (left == Type::Float && right == Type::Float) {
                emit("movq xmm0, rax"); emit("movq xmm1, rdi"); emit("ucomisd xmm0, xmm1");
                if (op == "==") { emit("sete al"); emit("setnp dil"); emit("and al, dil"); }
                else { emit("setne al"); emit("setp dil"); emit("or al, dil"); }
                emit("movzx eax, al");
            } else {
                emit("cmp rax, rdi"); emit(op == "==" ? "sete al" : "setne al"); emit("movzx eax, al");
            }
            machine_store(plan, instruction.result); return;
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (left == Type::Float && right == Type::Float) {
                emit("movq xmm0, rax"); emit("movq xmm1, rdi");
                if (op == "<") { emit("ucomisd xmm1, xmm0"); emit("seta al"); }
                else if (op == "<=") { emit("ucomisd xmm1, xmm0"); emit("setae al"); }
                else if (op == ">") { emit("ucomisd xmm0, xmm1"); emit("seta al"); }
                else { emit("ucomisd xmm0, xmm1"); emit("setae al"); }
                emit("movzx eax, al");
            } else {
                emit("cmp rax, rdi");
                if (op == "<") emit("setl al"); else if (op == "<=") emit("setle al");
                else if (op == ">") emit("setg al"); else emit("setge al");
                emit("movzx eax, al");
            }
            machine_store(plan, instruction.result); return;
        }
        if (left == Type::Int && right == Type::Int) {
            if (op == "&" || op == "|" || op == "^")
                emit(std::string(op == "&" ? "and" : op == "|" ? "or" : "xor") + " rax, rdi");
            else if (op == "<<" || op == ">>") {
                emit("mov rcx, rdi"); emit(std::string(op == "<<" ? "shl" : "sar") + " rax, cl");
            } else {
                emit("mov rsi, rdi"); emit("mov rdi, rax");
                static const std::unordered_map<std::string, std::string> ops = {
                    {"+", "pp_add_i64"}, {"-", "pp_sub_i64"}, {"*", "pp_mul_i64"},
                    {"/", "pp_div_i64"}, {"%", "pp_mod_i64"}};
                const auto found = ops.find(op);
                if (found == ops.end()) internal("unsupported integer Machine IR binary operation '" + op + "'");
                emit("call " + found->second + "@PLT");
            }
        } else if (left == Type::Str && right == Type::Str && op == "+") {
            emit("mov rsi, rdi"); emit("mov rdi, rax"); emit("call pp_concat@PLT");
        } else if (left == Type::Float && right == Type::Float) {
            emit("movq xmm0, rax"); emit("movq xmm1, rdi");
            if (op == "+") emit("addsd xmm0, xmm1"); else if (op == "-") emit("subsd xmm0, xmm1");
            else if (op == "*") emit("mulsd xmm0, xmm1"); else if (op == "/") emit("divsd xmm0, xmm1");
            else internal("unsupported float Machine IR binary operation '" + op + "'");
            emit("movq rax, xmm0");
        } else internal("unsupported Machine IR binary operand types");
        machine_store(plan, instruction.result);
    }

    void machine_emit_print(const std::string &name, const Type &type,
                            ppmachine::VReg value, const MachineFramePlan &plan) {
        machine_load(plan, value, "rax");
        const std::string prefix = "pp_" + name + "_";
        if (type == Type::Float) { emit("movq xmm0, rax"); emit("call " + prefix + "float@PLT"); }
        else if (type == Type::Bool) { emit("mov rdi, rax"); emit("call " + prefix + "bool@PLT"); }
        else if (type == Type::Str) { emit("mov rdi, rax"); emit("call " + prefix + "str@PLT"); }
        else { emit("mov rdi, rax"); emit("call " + prefix + "int@PLT"); }
    }

    void machine_load_builtin_gpr_args(const ppmachine::Instruction &instruction,
                                       const MachineFramePlan &plan) {
        static const std::vector<std::string> regs{"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        if (instruction.operands.size() > regs.size()) internal("builtin has too many direct runtime arguments");
        for (std::size_t i = 0; i < instruction.operands.size(); ++i)
            machine_load(plan, instruction.operands[i], regs[i]);
    }

    void emit_machine_builtin(const ppmachine::Instruction &instruction,
                              const MachineFramePlan &plan) {
        const std::string &name = instruction.detail;
        if (name == "move") {
            if (instruction.result != ppmachine::NoValue) {
                machine_load(plan, instruction.operands.at(0), "rax");
                machine_store(plan, instruction.result);
            }
            return;
        }
        if (name == "drop") {
            const Type type = machine_value_type(plan, instruction.operands.at(0));
            if (is_record(type)) {
                machine_load(plan, instruction.operands.at(0), "rax");
                machine_drop_at_address(type, plan);
            } else if (type == Type::Nums) {
                machine_load(plan, instruction.operands.at(0), "rdi"); emit("call pp_numbers_free@PLT");
            } else if (needs_drop(type)) {
                machine_load(plan, instruction.operands.at(0), "rdi"); emit("call pp_object_free@PLT");
            }
            return;
        }
        if (name == "print" || name == "println") {
            machine_emit_print(name, machine_value_type(plan, instruction.operands.at(0)), instruction.operands.at(0), plan);
            return;
        }
        if (name == "decimal") {
            machine_load(plan, instruction.operands.at(0), "rdi"); emit("call pp_decimal@PLT"); emit("movq rax, xmm0");
        } else if (name == "whole") {
            machine_load(plan, instruction.operands.at(0), "rax"); emit("movq xmm0, rax"); emit("call pp_whole@PLT");
        } else {
            const BuiltinSpec *builtin = nullptr;
            for (const BuiltinSpec &candidate : punpun_builtins()) if (candidate.name == name) { builtin = &candidate; break; }
            if (!builtin || builtin->runtime_symbol.empty()) internal("unknown Machine IR builtin '" + name + "'");
            machine_load_builtin_gpr_args(instruction, plan);
            emit("call " + builtin->runtime_symbol + "@PLT");
        }
        if (instruction.result != ppmachine::NoValue) {
            if (instruction.type == Type::Bool) emit("movzx eax, al");
            machine_store(plan, instruction.result);
        }
    }

    void emit_machine_native_call(const ppmachine::Instruction &instruction,
                                  const MachineFramePlan &plan) {
        if (instruction.operands.size() != instruction.call.arguments.size())
            internal("native Machine IR call argument count mismatch");
        std::size_t stack_bytes = 0;
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            const auto &argument = instruction.call.arguments[i];
            if (argument.location.kind == ppmachine::AbiLocationKind::Register) {
                machine_load(plan, instruction.operands[i], "rax");
                if (argument.location.name.rfind("xmm", 0) == 0)
                    emit("movq " + argument.location.name + ", rax");
                else emit("mov " + argument.location.name + ", rax");
            } else if (argument.location.kind == ppmachine::AbiLocationKind::Stack) {
                stack_bytes = std::max(stack_bytes, argument.location.offset + argument.location.size);
            } else internal("unsupported native ABI argument location");
        }
        const std::size_t stack_frame = ((stack_bytes + 15) / 16) * 16;
        if (stack_frame) {
            emit("sub rsp, " + std::to_string(stack_frame));
            for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                const auto &argument = instruction.call.arguments[i];
                if (argument.location.kind != ppmachine::AbiLocationKind::Stack) continue;
                machine_load(plan, instruction.operands[i], "rax");
                emit("mov " + mem("rsp", static_cast<int64_t>(argument.location.offset)) + ", rax");
            }
        }
        const ppmachine::Function &callee = machine_function(instruction.detail);
        emit("call " + callee.native_symbol + "@PLT");
        if (stack_frame) emit("add rsp, " + std::to_string(stack_frame));
        if (instruction.result != ppmachine::NoValue) {
            if (instruction.type == Type::Float) emit("movq rax, xmm0");
            else if (instruction.type == Type::Bool) emit("movzx eax, al");
            machine_store(plan, instruction.result);
        }
    }

    void emit_machine_user_call(const ppmachine::Instruction &instruction,
                                const MachineFramePlan &plan) {
        if (instruction.operands.size() != instruction.call.arguments.size())
            internal("Machine IR call argument count mismatch");
        const ppmachine::Function &callee = machine_function(instruction.detail);
        if (instruction.call.hidden_result_pointer) {
            const int64_t destination = machine_aggregate_storage(plan, instruction.result);
            emit("lea rax, " + mem_rbp(destination));
            emit("mov " + mem_rbp(plan.call_area + static_cast<int64_t>(instruction.call.result.location.offset)) + ", rax");
        }
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            const auto &argument = instruction.call.arguments[i];
            if (argument.location.kind != ppmachine::AbiLocationKind::ArgumentBlock)
                internal("PunPun call argument is not in argument block");
            const Type actual = machine_value_type(plan, instruction.operands[i]);
            const int64_t destination = plan.call_area + static_cast<int64_t>(argument.location.offset);
            if (is_record(actual) && !is_reference_type(argument.type))
                machine_copy_value_to_frame(plan, instruction.operands[i], actual, destination);
            else {
                machine_load(plan, instruction.operands[i], "rax");
                emit("mov " + mem_rbp(destination) + ", rax");
            }
        }
        if (callee.is_async) {
            emit("lea rdi, [rip + " + function_symbol(instruction.detail) + "]");
            if (instruction.call.argument_block_size) emit("lea rsi, " + mem_rbp(plan.call_area));
            else emit("xor esi, esi");
            emit("mov rdx, " + std::to_string(instruction.call.argument_block_size));
            emit("call pp_task_spawn@PLT");
        } else {
            if (instruction.call.argument_block_size) emit("lea rdi, " + mem_rbp(plan.call_area));
            else emit("xor edi, edi");
            emit("call " + function_symbol(instruction.detail));
        }
        if (instruction.result != ppmachine::NoValue) {
            if (instruction.call.hidden_result_pointer) {
                machine_set_result_address(plan, instruction.result, machine_aggregate_storage(plan, instruction.result));
            } else {
                if (instruction.type == Type::Bool) emit("movzx eax, al");
                machine_store(plan, instruction.result);
            }
        }
    }

    void emit_machine_call(const ppmachine::Instruction &instruction,
                           const MachineFramePlan &plan) {
        if (!instruction.call.known_function) {
            emit_machine_builtin(instruction, plan);
            return;
        }
        if (instruction.call.convention == ppmachine::CallingConvention::SysVAMD64)
            emit_machine_native_call(instruction, plan);
        else emit_machine_user_call(instruction, plan);
    }

    void emit_machine_construct(const ppmachine::Instruction &instruction,
                                const MachineFramePlan &plan) {
        const Shape &definition = *shapes_.at(instruction.type.name);
        const ShapeLayout &shape = layout(instruction.type.name);
        if (!definition.reference_type) {
            const int64_t destination = machine_aggregate_storage(plan, instruction.result);
            for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                const Parameter &field = definition.fields.at(i);
                const int64_t offset = shape.fields.at(field.name).offset;
                machine_copy_value_to_frame(plan, instruction.operands[i], field.type, destination + offset);
            }
            machine_set_result_address(plan, instruction.result, destination);
            return;
        }
        emit("mov rdi, " + std::to_string(shape.size));
        emit("call pp_object_alloc@PLT");
        machine_store(plan, instruction.result);
        if (!definition.initializer_name.empty()) {
            const ppmachine::Function &initializer = machine_function(definition.initializer_name);
            if (initializer.abi.parameters.size() != instruction.operands.size() + 1)
                internal("initializer Machine IR ABI parameter mismatch");
            machine_load(plan, instruction.result, "rax");
            emit("mov " + mem_rbp(plan.call_area + static_cast<int64_t>(initializer.abi.parameters[0].location.offset)) + ", rax");
            for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                const Type actual = machine_value_type(plan, instruction.operands[i]);
                const int64_t destination = plan.call_area + static_cast<int64_t>(initializer.abi.parameters[i + 1].location.offset);
                if (is_record(actual)) machine_copy_value_to_frame(plan, instruction.operands[i], actual, destination);
                else { machine_load(plan, instruction.operands[i], "rax"); emit("mov " + mem_rbp(destination) + ", rax"); }
            }
            emit("lea rdi, " + mem_rbp(plan.call_area));
            emit("call " + function_symbol(definition.initializer_name));
        }
    }

    static std::size_t enum_variant_index(const Shape &definition, const std::string &name) {
        for (std::size_t i = 0; i < definition.enum_variants.size(); ++i)
            if (definition.enum_variants[i].name == name) return i;
        throw Error("internal compiler error: unknown enum variant '" + definition.name + "::" + name + "'");
    }

    void emit_machine_enum_construct(const ppmachine::Instruction &instruction,
                                     const MachineFramePlan &plan) {
        const Shape &definition = *shapes_.at(instruction.type.name);
        const ShapeLayout &shape = layout(instruction.type.name);
        const std::size_t split = instruction.detail.rfind("::");
        if (split == std::string::npos) internal("enum Machine IR detail has no variant");
        const std::string variant_name = instruction.detail.substr(split + 2);
        const std::size_t index = enum_variant_index(definition, variant_name);
        const int64_t destination = machine_aggregate_storage(plan, instruction.result);
        for (int64_t offset = 0; offset < shape.size; offset += 8)
            emit("mov qword ptr " + mem_rbp(destination + offset) + ", 0");
        emit("mov qword ptr " + mem_rbp(destination + shape.fields.at("__tag").offset) + ", " + std::to_string(index));
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            const Type payload = definition.enum_variants.at(index).payload.at(i);
            const FieldInfo &field = shape.fields.at("__v" + std::to_string(index) + "_" + std::to_string(i));
            machine_copy_value_to_frame(plan, instruction.operands[i], payload, destination + field.offset);
        }
        machine_set_result_address(plan, instruction.result, destination);
    }

    void emit_machine_member(const ppmachine::Instruction &instruction,
                             const MachineFramePlan &plan) {
        Type base_type = machine_value_type(plan, instruction.operands.at(0));
        if (is_reference_type(base_type)) base_type = pointee_type(base_type);
        const ShapeLayout &shape = layout(base_type.name);
        const FieldInfo &field = shape.fields.at(instruction.detail);
        machine_load(plan, instruction.operands.at(0), "rax");
        if (is_record(field.type)) {
            if (field.offset) emit("add rax, " + std::to_string(field.offset));
        } else emit("mov rax, " + mem("rax", field.offset));
        machine_store(plan, instruction.result);
    }

    void emit_machine_address(const ppmachine::Instruction &instruction,
                              const MachineFramePlan &plan) {
        const std::string target = storage_from_address_detail(instruction.detail);
        if (!target.empty() && target.front() != '.') {
            const auto local = plan.locals.find(target);
            if (local == plan.locals.end()) internal("address.of references unknown local '" + target + "'");
            emit("lea rax, " + mem_rbp(local->second.offset));
        } else {
            if (instruction.operands.empty() || target.size() < 2) internal("malformed member address in Machine IR");
            Type base_type = machine_value_type(plan, instruction.operands.at(0));
            if (is_reference_type(base_type)) base_type = pointee_type(base_type);
            const FieldInfo &field = layout(base_type.name).fields.at(target.substr(1));
            machine_load(plan, instruction.operands.at(0), "rax");
            if (field.offset) emit("add rax, " + std::to_string(field.offset));
        }
        machine_store(plan, instruction.result);
    }

    void emit_machine_instruction(const ppmachine::Instruction &instruction,
                                  const ppmachine::Function &function,
                                  const MachineFramePlan &plan) {
        debug_location(instruction.token);
        switch (instruction.op) {
            case pphir::Op::Parameter: {
                const auto parameter = plan.parameter_indices.find(instruction.result);
                if (parameter == plan.parameter_indices.end() || parameter->second >= function.abi.parameters.size())
                    internal("Machine IR parameter has no ABI slot: '" + instruction.detail + "'");
                const auto &abi = function.abi.parameters.at(parameter->second);
                emit("mov rcx, " + mem_rbp(plan.args_pointer));
                if (is_record(instruction.type)) emit("lea rax, " + mem("rcx", static_cast<int64_t>(abi.location.offset)));
                else emit("mov rax, " + mem("rcx", static_cast<int64_t>(abi.location.offset)));
                machine_store(plan, instruction.result); return;
            }
            case pphir::Op::Constant: emit_machine_constant(instruction, plan); return;
            case pphir::Op::Load:
            case pphir::Op::MoveLoad: {
                const auto found = plan.locals.find(instruction.detail);
                if (found == plan.locals.end()) internal("Machine IR load references unknown local '" + instruction.detail + "'");
                if (is_record(found->second.type)) emit("lea rax, " + mem_rbp(found->second.offset));
                else emit("mov rax, " + mem_rbp(found->second.offset));
                machine_store(plan, instruction.result);
                if (instruction.op == pphir::Op::MoveLoad && found->second.alive_offset)
                    emit("mov qword ptr " + mem_rbp(found->second.alive_offset) + ", 0");
                return;
            }
            case pphir::Op::Store: {
                const auto found = plan.locals.find(instruction.detail);
                if (found == plan.locals.end()) internal("Machine IR store references unknown local '" + instruction.detail + "'");
                machine_copy_value_to_frame(plan, instruction.operands.at(0), found->second.type, found->second.offset);
                if (found->second.alive_offset) emit("mov qword ptr " + mem_rbp(found->second.alive_offset) + ", 1");
                return;
            }
            case pphir::Op::Drop: machine_drop_local(plan, instruction.detail); return;
            case pphir::Op::Call: emit_machine_call(instruction, plan); return;
            case pphir::Op::Construct: emit_machine_construct(instruction, plan); return;
            case pphir::Op::EnumConstruct: emit_machine_enum_construct(instruction, plan); return;
            case pphir::Op::Match:
            case pphir::Op::Propagate:
                internal("high-level match/propagate survived HIR CFG lowering");
            case pphir::Op::Await:
                machine_load(plan, instruction.operands.at(0), "rdi"); emit("call pp_task_await_bits@PLT");
                if (instruction.result != ppmachine::NoValue) machine_store(plan, instruction.result);
                return;
            case pphir::Op::Unary: emit_machine_unary(instruction, plan); return;
            case pphir::Op::Binary: emit_machine_binary(instruction, plan); return;
            case pphir::Op::Member: emit_machine_member(instruction, plan); return;
            case pphir::Op::Index:
                machine_load(plan, instruction.operands.at(0), "rdi"); machine_load(plan, instruction.operands.at(1), "rsi");
                emit("call pp_at@PLT"); machine_store(plan, instruction.result); return;
            case pphir::Op::List:
                emit("call pp_numbers_new@PLT"); machine_store(plan, instruction.result);
                for (ppmachine::VReg operand : instruction.operands) {
                    machine_load(plan, instruction.result, "rdi"); machine_load(plan, operand, "rsi"); emit("call pp_push@PLT");
                }
                return;
            case pphir::Op::AddressOf: emit_machine_address(instruction, plan); return;
            case pphir::Op::Deref:
                machine_load(plan, instruction.operands.at(0), "rax");
                if (!is_record(instruction.type)) emit("mov rax, " + mem("rax", 0));
                machine_store(plan, instruction.result); return;
            case pphir::Op::SizeOf:
                emit("mov rax, " + std::to_string(type_size(Type{instruction.detail})));
                machine_store(plan, instruction.result); return;
            case pphir::Op::AlignOf:
                emit("mov rax, 8"); machine_store(plan, instruction.result); return;
            case pphir::Op::StoreMember: {
                Type base_type = machine_value_type(plan, instruction.operands.at(0));
                if (is_reference_type(base_type)) base_type = pointee_type(base_type);
                const FieldInfo &field = layout(base_type.name).fields.at(instruction.detail);
                machine_load(plan, instruction.operands.at(0), "rcx");
                if (is_record(field.type)) {
                    machine_load(plan, instruction.operands.at(1), "rsi");
                    for (int64_t k = 0; k < type_size(field.type); k += 8) {
                        emit("mov rax, " + mem("rsi", k)); emit("mov " + mem("rcx", field.offset + k) + ", rax");
                    }
                } else {
                    machine_load(plan, instruction.operands.at(1), "rax"); emit("mov " + mem("rcx", field.offset) + ", rax");
                }
                return;
            }
            case pphir::Op::StoreIndex:
                machine_load(plan, instruction.operands.at(0), "rdi"); machine_load(plan, instruction.operands.at(1), "rsi");
                machine_load(plan, instruction.operands.at(2), "rdx"); emit("call pp_put@PLT"); return;
            case pphir::Op::StoreIndirect:
                machine_load(plan, instruction.operands.at(0), "rcx");
                if (is_record(instruction.type)) {
                    machine_load(plan, instruction.operands.at(1), "rsi");
                    for (int64_t k = 0; k < type_size(instruction.type); k += 8) {
                        emit("mov rax, " + mem("rsi", k)); emit("mov " + mem("rcx", k) + ", rax");
                    }
                } else { machine_load(plan, instruction.operands.at(1), "rax"); emit("mov " + mem("rcx", 0) + ", rax"); }
                return;
            case pphir::Op::Say: {
                const auto value = instruction.operands.at(0);
                const Type type = machine_value_type(plan, value);
                machine_load(plan, value, "rax");
                if (type == Type::Float) { emit("movq xmm0, rax"); emit("call pp_println_float@PLT"); }
                else if (type == Type::Bool) { emit("mov rdi, rax"); emit("call pp_println_bool@PLT"); }
                else if (type == Type::Str) { emit("mov rdi, rax"); emit("call pp_println_str@PLT"); }
                else { emit("mov rdi, rax"); emit("call pp_println_int@PLT"); }
                return;
            }
        }
        internal("unsupported operation reached Machine IR body emitter");
    }

    void generate_machine_function(const ppmachine::Function &function) {
        MachineFramePlan plan = plan_machine_frame(function);
        body_.str(""); body_.clear(); ret_label_ = new_label();
        const int64_t saved_bytes = static_cast<int64_t>(function.callee_saved_registers.size() * 8);
        const int64_t frame_size = align_up(plan.frame + saved_bytes, 16) - saved_bytes;
        const std::string symbol = function_symbol(function.name);
        text_ << "# body-lowering: machine-ir @" << function.name << "\n";
        text_ << ".globl " << symbol << "\n";
        text_ << ".type " << symbol << ", @function\n" << symbol << ":\n";
        text_ << "    push rbp\n    mov rbp, rsp\n    sub rsp, " << frame_size << "\n";
        for (const std::string &reg : function.callee_saved_registers) text_ << "    push " << reg << "\n";
        text_ << "    mov " << mem_rbp(plan.args_pointer) << ", rdi\n";
        for (const auto &block : function.blocks) {
            const std::string block_label = ".Lmir_" + function_symbol(function.name) + "_bb" + std::to_string(block.id);
            body_ << block_label << ":\n";
            for (const auto &instruction : block.instructions) emit_machine_instruction(instruction, function, plan);
            const auto &term = block.terminator;
            if (term.kind == pphir::Terminator::Kind::Jump)
                emit("jmp .Lmir_" + function_symbol(function.name) + "_bb" + std::to_string(term.first));
            else if (term.kind == pphir::Terminator::Kind::Branch) {
                machine_load(plan, term.value, "rax"); emit("cmp rax, 0");
                emit("jne .Lmir_" + function_symbol(function.name) + "_bb" + std::to_string(term.first));
                emit("jmp .Lmir_" + function_symbol(function.name) + "_bb" + std::to_string(term.second));
            } else if (term.kind == pphir::Terminator::Kind::Return) {
                if (term.value != ppmachine::NoValue) {
                    if (function.abi.hidden_result_pointer) {
                        machine_load(plan, term.value, "rsi");
                        emit("mov rcx, " + mem_rbp(plan.args_pointer));
                        emit("mov rdi, " + mem("rcx", static_cast<int64_t>(function.abi.result.location.offset)));
                        for (int64_t k = 0; k < type_size(function.result); k += 8) {
                            emit("mov rax, " + mem("rsi", k)); emit("mov " + mem("rdi", k) + ", rax");
                        }
                        emit("mov rax, rdi");
                    } else machine_load(plan, term.value, "rax");
                }
                emit("jmp " + ret_label_);
            } else if (term.kind == pphir::Terminator::Kind::Unreachable) emit("ud2");
            else internal("unterminated Machine IR block reached x86 body emitter");
        }
        text_ << body_.str() << ret_label_ << ":\n";
        for (auto reg = function.callee_saved_registers.rbegin(); reg != function.callee_saved_registers.rend(); ++reg)
            text_ << "    pop " << *reg << "\n";
        text_ << "    add rsp, " << frame_size << "\n    pop rbp\n    ret\n";
        text_ << "    .size " << symbol << ", .-" << symbol << "\n";
    }


    // -- process entry -----------------------------------------------------
    void generate_entry() {
        text_ << ".globl main\n";
        text_ << ".type main, @function\n";
        text_ << "main:\n";
        text_ << "    push rbp\n";
        text_ << "    mov rbp, rsp\n";
        text_ << "    call pp_runtime_init@PLT\n";  // argc/argv already in rdi/rsi
        text_ << "    xor edi, edi\n";
        text_ << "    call pp_fn_main\n";
        text_ << "    pop rbp\n";
        text_ << "    ret\n";
    }


};

#endif  // PUNPUN_BACKEND_X86_64_HPP
