// Punpun 0.3 native x86-64 backend.
//
// Emits GNU-assembler (Intel syntax) for System V AMD64 Linux and links against
// the same C/Rust/asm runtime the C backend uses. The frontend has already been
// type-checked by the C backend before this runs, so this pass re-derives types
// only to pick instructions; it assumes a well-formed program.
//
// Conventions
// -----------
//   * Scalars (int/bool/str/nums, and float as its 64-bit pattern) are produced
//     in RAX. A record expression produces the ADDRESS of its bytes in RAX.
//   * Every intermediate that must survive another sub-evaluation is spilled to
//     a fresh stack slot (a bump allocator that never reuses slots inside a
//     function), so no register allocation or liveness analysis is needed and
//     RSP stays 16-byte aligned across the whole body.
//   * Runtime helpers use the ordinary SysV ABI. Punpun-to-Punpun calls use a
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

class X86Backend {
  public:
    explicit X86Backend(const std::vector<Module> &modules) : modules_(modules) {
        for (const BuiltinSpec &builtin : punpun_builtins()) is_builtin_.insert(builtin.name);
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
        text_ << ".intel_syntax noprefix\n";
        int debug_id = 1;
        for (const auto &module : modules_) {
            const std::string key = module.file.lexically_normal().string();
            if (debug_files_.count(key)) continue;
            debug_files_[key] = debug_id;
            text_ << ".file " << debug_id++ << " \"" << asm_escaped(key) << "\"\n";
        }
        text_ << ".text\n";
        for (const auto &module : modules_)
            for (const auto &function : module.functions) if (!function.is_extern_native) generate_function(function);
        generate_entry();
        std::ostringstream out;
        out << text_.str();
        out << ".section .rodata\n" << rodata_.str();
        out << ".section .note.GNU-stack,\"\",@progbits\n";
        return out.str();
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
    };

    const std::vector<Module> &modules_;
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
    std::vector<std::pair<std::string, std::string>> loops_;  // {break, continue}
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
        for (unsigned char c : name) result += (std::isalnum(c) || c == '_') ? static_cast<char>(c) : '_';
        return result;
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

    // -- function emission --------------------------------------------------
    void generate_function(const Function &function) {
        frame_ = 0;
        scopes_.clear();
        scopes_.push_back({});
        loops_.clear();
        body_.str("");
        body_.clear();
        current_result_ = function.result;
        result_is_record_ = is_record(function.result);
        args_ptr_off_ = alloc(8);
        ret_label_ = new_label();

        int64_t block_offset = result_is_record_ ? 8 : 0;
        for (const auto &parameter : function.parameters) {
            scopes_.back()[parameter.name] = {parameter.type, block_offset, true};
            block_offset += type_size(parameter.type);
        }

        for (const auto &statement : function.body) gen_statement(statement);
        if (function.name == "main") emit("xor eax, eax");

        const int64_t frame_size = align_up(frame_, 16);
        const std::string symbol = function_symbol(function.name);
        text_ << ".type " << symbol << ", @function\n";
        text_ << symbol << ":\n";
        text_ << "    push rbp\n";
        text_ << "    mov rbp, rsp\n";
        text_ << "    sub rsp, " << frame_size << "\n";
        text_ << "    mov " << mem_rbp(args_ptr_off_) << ", rdi\n";
        text_ << body_.str();
        text_ << ret_label_ << ":\n";
        text_ << "    leave\n";
        text_ << "    ret\n";
        text_ << "    .size " << symbol << ", .-" << symbol << "\n";
    }

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

    // -- statements ---------------------------------------------------------
    void gen_statement(const Stmt &statement) {
        debug_location(statement.token);
        switch (statement.kind) {
            case Stmt::Kind::Variable: gen_variable(statement); break;
            case Stmt::Kind::Assign: gen_assign(statement); break;
            case Stmt::Kind::Expression: gen_expression(*statement.expression); break;
            case Stmt::Kind::Say: gen_say(statement); break;
            case Stmt::Kind::Return: gen_return(statement); break;
            case Stmt::Kind::If: gen_if(statement); break;
            case Stmt::Kind::While: gen_while(statement); break;
            case Stmt::Kind::Each: gen_each(statement); break;
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue: gen_break_continue(statement); break;
            case Stmt::Kind::Unsafe: gen_block(statement.body); break;
        }
    }

    void gen_variable(const Stmt &statement) {
        const Type value_type = gen_expression(*statement.expression);
        const Type type = statement.declared_type == Type::Infer ? value_type : statement.declared_type;
        const int64_t slot = alloc(type_size(type));
        if (is_record(type)) {
            emit("mov rsi, rax");
            copy_rsi_to_frame(slot, type_size(type));
        } else {
            emit("mov " + mem_rbp(slot) + ", rax");
        }
        scopes_.back()[statement.name] = {type, slot, false};
    }

    void gen_assign(const Stmt &statement) {
        const bool compound = statement.assignment_op != "=" && statement.assignment_op != "<-";
        if (statement.target->kind == Expr::Kind::Index) {
            const Expr &target = *statement.target;
            gen_expression(*target.children[0]);  // nums pointer
            const int64_t list_slot = alloc(8);
            emit("mov " + mem_rbp(list_slot) + ", rax");
            gen_expression(*target.children[1]);  // index
            const int64_t index_slot = alloc(8);
            emit("mov " + mem_rbp(index_slot) + ", rax");
            gen_expression(*statement.expression);
            const int64_t value_slot = alloc(8);
            emit("mov " + mem_rbp(value_slot) + ", rax");
            if (compound) {
                emit("mov rdi, " + mem_rbp(list_slot));
                emit("mov rsi, " + mem_rbp(index_slot));
                emit("call pp_at@PLT");
                emit("mov rdi, rax");
                emit("mov rsi, " + mem_rbp(value_slot));
                static const std::unordered_map<std::string, std::string> ops = {
                    {"+=", "pp_add_i64"}, {"-=", "pp_sub_i64"},
                    {"*=", "pp_mul_i64"}, {"/=", "pp_div_i64"}};
                emit("call " + ops.at(statement.assignment_op) + "@PLT");
                emit("mov " + mem_rbp(value_slot) + ", rax");
            }
            emit("mov rdx, " + mem_rbp(value_slot));
            emit("mov rdi, " + mem_rbp(list_slot));
            emit("mov rsi, " + mem_rbp(index_slot));
            emit("call pp_put@PLT");
            return;
        }
        const Type type = gen_lvalue_address(*statement.target);  // address in rax
        const int64_t addr_slot = alloc(8);
        emit("mov " + mem_rbp(addr_slot) + ", rax");
        gen_expression(*statement.expression);  // value or record address
        if (compound) {
            if (type == Type::Int) {
                emit("mov rsi, rax");
                emit("mov rcx, " + mem_rbp(addr_slot));
                emit("mov rdi, " + mem("rcx", 0));
                static const std::unordered_map<std::string, std::string> ops = {
                    {"+=", "pp_add_i64"}, {"-=", "pp_sub_i64"},
                    {"*=", "pp_mul_i64"}, {"/=", "pp_div_i64"}};
                emit("call " + ops.at(statement.assignment_op) + "@PLT");
            } else if (type == Type::Float) {
                emit("movq xmm1, rax");
                emit("mov rcx, " + mem_rbp(addr_slot));
                emit("movq xmm0, " + mem("rcx", 0));
                if (statement.assignment_op == "+=") emit("addsd xmm0, xmm1");
                else if (statement.assignment_op == "-=") emit("subsd xmm0, xmm1");
                else if (statement.assignment_op == "*=") emit("mulsd xmm0, xmm1");
                else emit("divsd xmm0, xmm1");
                emit("movq rax, xmm0");
            } else internal("compound assignment reached non-numeric type");
        }
        emit("mov rdi, " + mem_rbp(addr_slot));
        if (is_record(type)) {
            emit("mov rsi, rax");
            copy_via_rdi_rsi(type_size(type));
        } else emit("mov " + mem("rdi", 0) + ", rax");
    }

    // Produce the address of an assignable location (scalar slot or record
    // bytes) in RAX; returns its type.
    Type gen_lvalue_address(const Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            const Var *variable = lookup(expression.value);
            if (!variable) internal("unknown lvalue '" + expression.value + "'");
            if (variable->is_param) {
                emit("mov rcx, " + mem_rbp(args_ptr_off_));
                emit("lea rax, " + mem("rcx", variable->offset));
            } else emit("lea rax, " + mem_rbp(variable->offset));
            return variable->type;
        }
        if (expression.kind == Expr::Kind::Member) {
            const Type source_type = expression.children[0]->inferred_type;
            Type base;
            if (is_object(source_type) || is_reference_type(source_type)) {
                base = gen_expression(*expression.children[0]); // RAX is payload/reference pointer
            } else base = gen_lvalue_address(*expression.children[0]);
            if (is_reference_type(base)) base = pointee_type(base);
            const ShapeLayout &shape = layout(base.name);
            const FieldInfo &field = shape.fields.at(expression.value);
            if (field.offset != 0) emit("add rax, " + std::to_string(field.offset));
            return field.type;
        }
        if (expression.kind == Expr::Kind::Unary && expression.value == "*") {
            const Type pointer = gen_expression(*expression.children[0]);
            if (!is_pointer_like_type(pointer)) internal("dereference lvalue is not a pointer");
            return pointee_type(pointer);
        }
        internal("unsupported lvalue");
    }

    void gen_say(const Stmt &statement) {
        const Type type = gen_expression(*statement.expression);
        if (type == Type::Float) {
            emit("movq xmm0, rax");
            emit("call pp_println_float@PLT");
        } else if (type == Type::Bool) {
            emit("mov rdi, rax");
            emit("call pp_println_bool@PLT");
        } else if (type == Type::Str) {
            emit("mov rdi, rax");
            emit("call pp_println_str@PLT");
        } else {
            emit("mov rdi, rax");
            emit("call pp_println_int@PLT");
        }
    }

    void gen_return(const Stmt &statement) {
        if (!statement.expression) {
            emit("jmp " + ret_label_);
            return;
        }
        gen_expression(*statement.expression);
        if (result_is_record_) {
            emit("mov rcx, " + mem_rbp(args_ptr_off_));
            emit("mov rdi, " + mem("rcx", 0));  // hidden destination pointer
            emit("mov rsi, rax");
            copy_via_rdi_rsi(type_size(current_result_));
            emit("mov rax, rdi");
        }
        emit("jmp " + ret_label_);
    }

    void gen_if(const Stmt &statement) {
        gen_expression(*statement.expression);
        emit("cmp rax, 0");
        const std::string end = new_label();
        if (statement.alternative.empty()) {
            emit("je " + end);
            gen_block(statement.body);
            label(end);
        } else {
            const std::string other = new_label();
            emit("je " + other);
            gen_block(statement.body);
            emit("jmp " + end);
            label(other);
            gen_block(statement.alternative);
            label(end);
        }
    }

    void gen_while(const Stmt &statement) {
        const std::string top = new_label();
        const std::string end = new_label();
        label(top);
        gen_expression(*statement.expression);
        emit("cmp rax, 0");
        emit("je " + end);
        loops_.push_back({end, top});
        gen_block(statement.body);
        loops_.pop_back();
        emit("jmp " + top);
        label(end);
    }

    void gen_each(const Stmt &statement) {
        const int64_t counter = alloc(8);
        gen_expression(*statement.expression);  // start
        emit("mov " + mem_rbp(counter) + ", rax");
        const int64_t limit = alloc(8);
        gen_expression(*statement.upper);  // end
        emit("mov " + mem_rbp(limit) + ", rax");
        const std::string top = new_label();
        const std::string cont = new_label();
        const std::string end = new_label();
        scopes_.push_back({{statement.name, {Type::Int, counter, false}}});
        loops_.push_back({end, cont});
        label(top);
        emit("mov rax, " + mem_rbp(counter));
        emit("cmp rax, " + mem_rbp(limit));
        emit("jge " + end);
        for (const auto &child : statement.body) gen_statement(child);
        label(cont);
        emit("mov rax, " + mem_rbp(counter));
        emit("add rax, 1");
        emit("mov " + mem_rbp(counter) + ", rax");
        emit("jmp " + top);
        label(end);
        loops_.pop_back();
        scopes_.pop_back();
    }

    void gen_break_continue(const Stmt &statement) {
        if (loops_.empty()) internal("loop control outside loop");
        const auto &labels = loops_.back();
        emit("jmp " + (statement.kind == Stmt::Kind::Break ? labels.first : labels.second));
    }

    void gen_block(const std::vector<Stmt> &statements) {
        scopes_.push_back({});
        for (const auto &statement : statements) gen_statement(statement);
        scopes_.pop_back();
    }

    // -- expressions (result in RAX; record -> address in RAX) --------------
    Type gen_expression(const Expr &expression) {
        switch (expression.kind) {
            case Expr::Kind::Integer: {
                emit("movabs rax, " + std::to_string(std::stoll(expression.value)));
                return Type::Int;
            }
            case Expr::Kind::Float: {
                double value = std::stod(expression.value);
                uint64_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                emit("movabs rax, " + hex_u64(bits));
                return Type::Float;
            }
            case Expr::Kind::String: {
                emit("lea rax, [rip + " + add_string(expression.value) + "]");
                return Type::Str;
            }
            case Expr::Kind::Boolean: {
                if (expression.value == "yes") emit("mov eax, 1");
                else emit("xor eax, eax");
                return Type::Bool;
            }
            case Expr::Kind::Variable: return gen_variable_load(expression);
            case Expr::Kind::Member: return gen_member(expression);
            case Expr::Kind::Index: return gen_index(expression);
            case Expr::Kind::List: return gen_list(expression);
            case Expr::Kind::Call: return gen_call(expression);
            case Expr::Kind::MethodCall: return gen_call(expression);
            case Expr::Kind::SizeOf:
                emit("mov rax, " + std::to_string(type_size(Type{expression.value})));
                return Type::Int;
            case Expr::Kind::AlignOf:
                emit("mov rax, 8");
                return Type::Int;
            case Expr::Kind::Unary: return gen_unary(expression);
            case Expr::Kind::Binary: return gen_binary(expression);
        }
        internal("unreachable expression");
    }

    Type gen_variable_load(const Expr &expression) {
        const Var *variable = lookup(expression.value);
        if (!variable) internal("unknown variable '" + expression.value + "'");
        if (variable->is_param) {
            emit("mov rcx, " + mem_rbp(args_ptr_off_));
            if (is_record(variable->type)) emit("lea rax, " + mem("rcx", variable->offset));
            else emit("mov rax, " + mem("rcx", variable->offset));
        } else {
            if (is_record(variable->type)) emit("lea rax, " + mem_rbp(variable->offset));
            else emit("mov rax, " + mem_rbp(variable->offset));
        }
        return variable->type;
    }

    Type gen_member(const Expr &expression) {
        Type base = gen_expression(*expression.children[0]);  // value-record address or object/reference pointer in rax
        if (is_reference_type(base)) base = pointee_type(base);
        const ShapeLayout &shape = layout(base.name);
        const FieldInfo &field = shape.fields.at(expression.value);
        if (is_record(field.type)) {
            if (field.offset != 0) emit("add rax, " + std::to_string(field.offset));
        } else {
            emit("mov rax, " + mem("rax", field.offset));
        }
        return field.type;
    }

    Type gen_index(const Expr &expression) {
        gen_expression(*expression.children[0]);  // nums pointer
        const int64_t list_slot = alloc(8);
        emit("mov " + mem_rbp(list_slot) + ", rax");
        gen_expression(*expression.children[1]);  // index
        emit("mov rsi, rax");
        emit("mov rdi, " + mem_rbp(list_slot));
        emit("call pp_at@PLT");
        return Type::Int;
    }

    Type gen_list(const Expr &expression) {
        emit("call pp_numbers_new@PLT");
        const int64_t list_slot = alloc(8);
        emit("mov " + mem_rbp(list_slot) + ", rax");
        for (const auto &child : expression.children) {
            gen_expression(*child);
            emit("mov rsi, rax");
            emit("mov rdi, " + mem_rbp(list_slot));
            emit("call pp_push@PLT");
        }
        emit("mov rax, " + mem_rbp(list_slot));
        return Type::Nums;
    }

    Type gen_unary(const Expr &expression) {
        if (expression.value == "await") {
            const Type task = gen_expression(*expression.children[0]);
            if (!is_task_type(task)) internal("non-task reached await after semantic analysis");
            emit("mov rdi, rax");
            emit("call pp_task_await_bits@PLT");
            return expression.inferred_type;
        }
        if (expression.value == "-" && expression.children[0]->kind == Expr::Kind::Integer &&
            expression.children[0]->value == "9223372036854775808") {
            emit("movabs rax, 0x8000000000000000");
            return Type::Int;
        }
        if (expression.value == "&" || expression.value == "&mut" || expression.value == "&raw") {
            (void)gen_lvalue_address(*expression.children[0]);
            return expression.inferred_type;
        }
        const Type operand = gen_expression(*expression.children[0]);
        if (expression.value == "*") {
            const Type inner = pointee_type(operand);
            // A by-value record expression is represented by its address. For
            // scalar/object-reference pointees, load the referenced 8-byte value.
            if (!is_record(inner)) emit("mov rax, " + mem("rax", 0));
            return inner;
        }
        if (expression.value == "not") {
            emit("xor rax, 1");
            return Type::Bool;
        }
        if (expression.value == "~") {
            emit("not rax");
            return Type::Int;
        }
        if (operand == Type::Int) {
            emit("mov rdi, rax");
            emit("call pp_neg_i64@PLT");
            return Type::Int;
        }
        // float negation: flip the sign bit
        emit("movabs rcx, 0x8000000000000000");
        emit("xor rax, rcx");
        return Type::Float;
    }

    Type gen_binary(const Expr &expression) {
        const std::string &op = expression.value;
        if (op == "and" || op == "or") return gen_short_circuit(expression);

        const Type left = gen_expression(*expression.children[0]);
        const int64_t left_slot = alloc(8);
        emit("mov " + mem_rbp(left_slot) + ", rax");
        const Type right = gen_expression(*expression.children[1]);
        const int64_t right_slot = alloc(8);
        emit("mov " + mem_rbp(right_slot) + ", rax");

        if (is_raw_pointer_type(left) && right == Type::Int && (op == "+" || op == "-")) {
            emit("mov rax, " + mem_rbp(right_slot));
            const int64_t stride = type_size(pointee_type(left));
            if (stride != 1) emit("imul rax, " + std::to_string(stride));
            emit("mov rcx, " + mem_rbp(left_slot));
            emit(std::string(op == "+" ? "add" : "sub") + " rcx, rax");
            emit("mov rax, rcx");
            return left;
        }

        const bool is_float = left == Type::Float && right == Type::Float;
        (void)right;

        if (op == "==" || op == "!=") {
            if (left == Type::Str) {
                emit("mov rdi, " + mem_rbp(left_slot));
                emit("mov rsi, " + mem_rbp(right_slot));
                emit("call pp_str_eq@PLT");
                if (op == "!=") emit("xor rax, 1");
                return Type::Bool;
            }
            if (is_float) {
                load_float_operands(left_slot, right_slot);
                emit("ucomisd xmm0, xmm1");
                if (op == "==") {
                    emit("sete al");
                    emit("setnp cl");
                    emit("and al, cl");
                } else {
                    emit("setne al");
                    emit("setp cl");
                    emit("or al, cl");
                }
                emit("movzx eax, al");
                return Type::Bool;
            }
            emit("mov rax, " + mem_rbp(left_slot));
            emit("cmp rax, " + mem_rbp(right_slot));
            emit(op == "==" ? "sete al" : "setne al");
            emit("movzx eax, al");
            return Type::Bool;
        }

        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (is_float) {
                gen_float_compare(op, left_slot, right_slot);
                return Type::Bool;
            }
            emit("mov rax, " + mem_rbp(left_slot));
            emit("cmp rax, " + mem_rbp(right_slot));
            if (op == "<") emit("setl al");
            else if (op == "<=") emit("setle al");
            else if (op == ">") emit("setg al");
            else emit("setge al");
            emit("movzx eax, al");
            return Type::Bool;
        }

        if (left == Type::Int) {
            if (op == "&" || op == "|" || op == "^") {
                emit("mov rax, " + mem_rbp(left_slot));
                emit(std::string(op == "&" ? "and" : op == "|" ? "or" : "xor") +
                     " rax, " + mem_rbp(right_slot));
                return Type::Int;
            }
            if (op == "<<" || op == ">>") {
                emit("mov rax, " + mem_rbp(left_slot));
                emit("mov rcx, " + mem_rbp(right_slot));
                emit(std::string(op == "<<" ? "shl" : "sar") + " rax, cl");
                return Type::Int;
            }
            emit("mov rdi, " + mem_rbp(left_slot));
            emit("mov rsi, " + mem_rbp(right_slot));
            static const std::unordered_map<std::string, std::string> ops = {
                {"+", "pp_add_i64"}, {"-", "pp_sub_i64"}, {"*", "pp_mul_i64"},
                {"/", "pp_div_i64"}, {"%", "pp_mod_i64"}};
            emit("call " + ops.at(op) + "@PLT");
            return Type::Int;
        }
        if (left == Type::Str && op == "+") {
            emit("mov rdi, " + mem_rbp(left_slot));
            emit("mov rsi, " + mem_rbp(right_slot));
            emit("call pp_concat@PLT");
            return Type::Str;
        }
        // float arithmetic
        load_float_operands(left_slot, right_slot);
        if (op == "+") emit("addsd xmm0, xmm1");
        else if (op == "-") emit("subsd xmm0, xmm1");
        else if (op == "*") emit("mulsd xmm0, xmm1");
        else emit("divsd xmm0, xmm1");
        emit("movq rax, xmm0");
        return Type::Float;
    }

    void load_float_operands(int64_t left_slot, int64_t right_slot) {
        emit("mov rax, " + mem_rbp(left_slot));
        emit("movq xmm0, rax");
        emit("mov rax, " + mem_rbp(right_slot));
        emit("movq xmm1, rax");
    }

    void gen_float_compare(const std::string &op, int64_t left_slot, int64_t right_slot) {
        load_float_operands(left_slot, right_slot);
        // Use the swap trick so every comparison is false for NaN operands.
        if (op == "<") {
            emit("ucomisd xmm1, xmm0");
            emit("seta al");
        } else if (op == "<=") {
            emit("ucomisd xmm1, xmm0");
            emit("setae al");
        } else if (op == ">") {
            emit("ucomisd xmm0, xmm1");
            emit("seta al");
        } else {
            emit("ucomisd xmm0, xmm1");
            emit("setae al");
        }
        emit("movzx eax, al");
    }

    Type gen_short_circuit(const Expr &expression) {
        const std::string &op = expression.value;
        gen_expression(*expression.children[0]);
        const int64_t result_slot = alloc(8);
        emit("mov " + mem_rbp(result_slot) + ", rax");
        const std::string skip = new_label();
        emit("cmp rax, 0");
        emit(op == "and" ? "je " + skip : "jne " + skip);
        gen_expression(*expression.children[1]);
        emit("mov " + mem_rbp(result_slot) + ", rax");
        label(skip);
        emit("mov rax, " + mem_rbp(result_slot));
        return Type::Bool;
    }

    // -- calls --------------------------------------------------------------
    Type gen_call(const Expr &expression) {
        if (shapes_.count(expression.value)) return gen_constructor(expression);
        if (is_builtin_.count(expression.value)) return gen_builtin(expression);
        if (native_symbols_.count(expression.value)) return gen_native_call(expression);
        return gen_user_call(expression);
    }

    Type gen_constructor(const Expr &expression) {
        const ShapeLayout &shape = layout(expression.value);
        const Shape *definition = shapes_.at(expression.value);
        if (!definition->reference_type) {
            const int64_t record_slot = alloc(shape.size);
            for (size_t i = 0; i < definition->fields.size(); ++i) {
                const Parameter &field = definition->fields[i];
                const int64_t offset = shape.fields.at(field.name).offset;
                const Type value_type = gen_expression(*expression.children[i]);
                if (is_record(value_type)) {
                    emit("mov rsi, rax");
                    copy_rsi_to_frame(record_slot + offset, type_size(value_type));
                } else emit("mov " + mem_rbp(record_slot + offset) + ", rax");
            }
            emit("lea rax, " + mem_rbp(record_slot));
            return Type{expression.value};
        }

        // Identity object: allocate its payload once, then invoke the flattened
        // init method with the object pointer as the first argument.
        emit("mov rdi, " + std::to_string(shape.size));
        emit("call pp_object_alloc@PLT");
        const int64_t object_slot = alloc(8);
        emit("mov " + mem_rbp(object_slot) + ", rax");
        if (!definition->initializer_name.empty()) {
            const Signature &signature = user_signatures_.at(definition->initializer_name);
            int64_t block_size = 0;
            std::vector<int64_t> offsets;
            for (const Type &parameter : signature.parameters) {
                offsets.push_back(block_size);
                block_size += type_size(parameter);
            }
            const int64_t block_off = alloc(align_up(block_size, 8));
            emit("mov rax, " + mem_rbp(object_slot));
            emit("mov " + mem_rbp(block_off + offsets[0]) + ", rax");
            for (size_t i = 0; i < expression.children.size(); ++i) {
                const Type value_type = gen_expression(*expression.children[i]);
                const int64_t destination = block_off + offsets[i + 1];
                if (is_record(value_type)) {
                    emit("mov rsi, rax");
                    copy_rsi_to_frame(destination, type_size(value_type));
                } else emit("mov " + mem_rbp(destination) + ", rax");
            }
            emit("lea rdi, " + mem_rbp(block_off));
            emit("call " + function_symbol(definition->initializer_name));
        }
        emit("mov rax, " + mem_rbp(object_slot));
        return Type{expression.value};
    }

    Type gen_native_call(const Expr &expression) {
        const Signature &signature = user_signatures_.at(expression.value);
        static const char *registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        if (expression.children.size() > 6) internal("extern native call exceeded current scalar SysV argument limit");
        std::vector<int64_t> slots;
        for (const auto &child : expression.children) {
            const Type type = gen_expression(*child);
            if (is_record(type) || type == Type::Float) internal("unsupported direct native ABI value after semantic analysis");
            const int64_t slot = alloc(8);
            emit("mov " + mem_rbp(slot) + ", rax");
            slots.push_back(slot);
        }
        for (std::size_t i = 0; i < slots.size(); ++i) emit(std::string("mov ") + registers[i] + ", " + mem_rbp(slots[i]));
        emit("call " + native_symbols_.at(expression.value) + "@PLT");
        if (signature.result == Type::Bool) emit("movzx eax, al");
        return signature.result;
    }

    Type gen_user_call(const Expr &expression) {
        const Signature &signature = user_signatures_.at(expression.value);
        const bool returns_record = is_record(signature.result);

        int64_t block_size = returns_record ? 8 : 0;
        std::vector<int64_t> offsets;
        for (const Type &parameter : signature.parameters) {
            offsets.push_back(block_size);
            block_size += type_size(parameter);
        }

        int64_t dest_slot = 0;
        if (returns_record) dest_slot = alloc(type_size(signature.result));
        int64_t block_off = 0;
        if (block_size > 0) block_off = alloc(align_up(block_size, 8));

        if (returns_record) {
            emit("lea rax, " + mem_rbp(dest_slot));
            emit("mov " + mem_rbp(block_off) + ", rax");
        }
        for (size_t i = 0; i < expression.children.size(); ++i) {
            const Type value_type = gen_expression(*expression.children[i]);
            const Type expected = signature.parameters[i];
            const int64_t destination = block_off + offsets[i];
            if (is_reference_type(expected) && is_record(value_type)) {
                // Record expressions already produce their address. A method
                // self-reference therefore costs one pointer store, not a copy.
                emit("mov " + mem_rbp(destination) + ", rax");
            } else if (is_record(value_type)) {
                emit("mov rsi, rax");
                copy_rsi_to_frame(destination, type_size(value_type));
            } else {
                emit("mov " + mem_rbp(destination) + ", rax");
            }
        }
        if (signature.is_async) {
            emit("lea rdi, [rip + " + function_symbol(expression.value) + "]");
            if (block_size > 0) emit("lea rsi, " + mem_rbp(block_off));
            else emit("xor esi, esi");
            emit("mov rdx, " + std::to_string(block_size));
            emit("call pp_task_spawn@PLT");
            return task_type(signature.result);
        }
        if (block_size > 0) emit("lea rdi, " + mem_rbp(block_off));
        else emit("xor edi, edi");
        emit("call " + function_symbol(expression.value));
        if (returns_record) {
            emit("lea rax, " + mem_rbp(dest_slot));
            return signature.result;
        }
        if (signature.result == Type::Bool) emit("movzx eax, al");
        return signature.result;
    }

    Type gen_builtin(const Expr &expression) {
        const std::string &name = expression.value;
        std::vector<int64_t> slots;
        std::vector<Type> types;
        for (const auto &child : expression.children) {
            const Type type = gen_expression(*child);
            const int64_t slot = alloc(8);
            emit("mov " + mem_rbp(slot) + ", rax");
            slots.push_back(slot);
            types.push_back(type);
        }
        const auto load = [&](const std::string &reg, size_t index) {
            emit("mov " + reg + ", " + mem_rbp(slots[index]));
        };

        if (name == "move") {
            load("rax", 0);
            return types.at(0);
        }
        if (name == "drop") {
            load("rdi", 0);
            emit(std::string("call ") + (types.at(0) == Type::Nums ? "pp_numbers_free@PLT" : "pp_object_free@PLT"));
            return Type::Void;
        }

        if (name == "print" || name == "println") {
            const std::string prefix = "pp_" + name + "_";
            if (types[0] == Type::Float) {
                load("rax", 0);
                emit("movq xmm0, rax");
                emit("call " + prefix + "float@PLT");
            } else if (types[0] == Type::Bool) {
                load("rdi", 0);
                emit("call " + prefix + "bool@PLT");
            } else if (types[0] == Type::Str) {
                load("rdi", 0);
                emit("call " + prefix + "str@PLT");
            } else {
                load("rdi", 0);
                emit("call " + prefix + "int@PLT");
            }
            return Type::Void;
        }
        if (name == "len") { load("rdi", 0); emit("call pp_len@PLT"); return Type::Int; }
        if (name == "abs") { load("rdi", 0); emit("call pp_abs_i64@PLT"); return Type::Int; }
        if (name == "clock_ms") { emit("call pp_clock_ms@PLT"); return Type::Int; }
        if (name == "panic") { load("rdi", 0); emit("call pp_panic@PLT"); return Type::Void; }
        if (name == "numbers") { emit("call pp_numbers_new@PLT"); return Type::Nums; }
        if (name == "push") { load("rdi", 0); load("rsi", 1); emit("call pp_push@PLT"); return Type::Void; }
        if (name == "at") { load("rdi", 0); load("rsi", 1); emit("call pp_at@PLT"); return Type::Int; }
        if (name == "put") { load("rdi", 0); load("rsi", 1); load("rdx", 2); emit("call pp_put@PLT"); return Type::Void; }
        if (name == "size") { load("rdi", 0); emit("call pp_size@PLT"); return Type::Int; }
        if (name == "pop") { load("rdi", 0); emit("call pp_pop@PLT"); return Type::Int; }
        if (name == "sort") { load("rdi", 0); emit("call pp_sort@PLT"); return Type::Void; }
        if (name == "concat") { load("rdi", 0); load("rsi", 1); emit("call pp_concat@PLT"); return Type::Str; }
        if (name == "slice") { load("rdi", 0); load("rsi", 1); load("rdx", 2); emit("call pp_slice@PLT"); return Type::Str; }
        if (name == "contains") { load("rdi", 0); load("rsi", 1); emit("call pp_contains@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "read_text") { load("rdi", 0); emit("call pp_read_text@PLT"); return Type::Str; }
        if (name == "write_text") { load("rdi", 0); load("rsi", 1); emit("call pp_write_text@PLT"); return Type::Void; }
        if (name == "text") { load("rdi", 0); emit("call pp_text_int@PLT"); return Type::Str; }
        if (name == "parse_int") { load("rdi", 0); emit("call pp_parse_int@PLT"); return Type::Int; }
        if (name == "decimal") { load("rdi", 0); emit("call pp_decimal@PLT"); emit("movq rax, xmm0"); return Type::Float; }
        if (name == "whole") { load("rax", 0); emit("movq xmm0, rax"); emit("call pp_whole@PLT"); return Type::Int; }
        if (name == "assert") { load("rdi", 0); load("rsi", 1); emit("call pp_assert@PLT"); return Type::Void; }
        if (name == "arg_count") { emit("call pp_arg_count@PLT"); return Type::Int; }
        if (name == "arg") { load("rdi", 0); emit("call pp_arg@PLT"); return Type::Str; }
        if (name == "file_exists") { load("rdi", 0); emit("call pp_file_exists@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "current_dir") { emit("call pp_current_dir@PLT"); return Type::Str; }
        if (name == "env_has") { load("rdi", 0); emit("call pp_env_has@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "env_or") { load("rdi", 0); load("rsi", 1); emit("call pp_env_or@PLT"); return Type::Str; }
        if (name == "platform") { emit("call pp_platform@PLT"); return Type::Str; }
        if (name == "read_line") { emit("call pp_read_line@PLT"); return Type::Str; }
        if (name == "sleep_ms") { load("rdi", 0); emit("call pp_sleep_ms@PLT"); return Type::Void; }
        if (name == "cancel") { load("rdi", 0); emit("call pp_task_cancel@PLT"); return Type::Void; }
        if (name == "task_done") { load("rdi", 0); emit("call pp_task_is_done@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "cancelled") { emit("call pp_task_cancelled@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "utf8_valid") { load("rdi", 0); emit("call pp_utf8_valid@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "utf8_len") { load("rdi", 0); emit("call pp_utf8_len@PLT"); return Type::Int; }
        if (name == "make_dir") { load("rdi", 0); emit("call pp_make_dir@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "remove_file") { load("rdi", 0); emit("call pp_remove_file@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "rename_file") { load("rdi", 0); load("rsi", 1); emit("call pp_rename_file@PLT"); emit("movzx eax, al"); return Type::Bool; }
        if (name == "path_join") { load("rdi", 0); load("rsi", 1); emit("call pp_path_join@PLT"); return Type::Str; }
        internal("unknown builtin '" + name + "'");
    }
};

#endif  // PUNPUN_BACKEND_X86_64_HPP
