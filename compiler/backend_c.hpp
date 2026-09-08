// PunPun portable C17 lowering backend.
//
// This backend is deliberately NOT a semantic analyzer.  The shared
// SemanticAnalyzer runs before every backend and annotates expressions with
// their resolved types.  Keeping C emission dumb prevents the portable path
// from becoming a second compiler with subtly different language rules.
#ifndef PUNPUN_BACKEND_C_HPP
#define PUNPUN_BACKEND_C_HPP

#include <cmath>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "builtins.hpp"
#include "frontend.hpp"

struct Generated {
    Type type;
    std::string code;
};

class CBackend {
  public:
    explicit CBackend(const std::vector<Module> &modules) : modules_(modules) {
        for (const BuiltinSpec &builtin : punpun_builtins()) {
            builtin_specs_[builtin.name] = &builtin;
            if (!builtin.runtime_symbol.empty()) builtin_symbols_[builtin.name] = builtin.runtime_symbol;
        }
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) shapes_[shape.name] = &shape;
            for (const Function &function : module.functions) functions_[function.name] = &function;
        }
    }

    std::string generate() {
        out_ << "#include \"punpun.h\"\n"
                "#include <stddef.h>\n"
                "#include <stdint.h>\n"
                "#include <string.h>\n\n";

        // Forward declarations make object/reference cycles legal while the
        // recursive definition pass still rejects impossible by-value cycles.
        for (const auto &[name, shape] : shapes_) {
            (void)shape;
            out_ << "typedef struct " << c_record_tag(name) << " " << c_record_tag(name) << ";\n";
        }
        if (!shapes_.empty()) out_ << "\n";

        for (const Module &module : modules_)
            for (const Shape &shape : module.shapes) generate_shape(shape);

        for (const Module &module : modules_)
            for (const Function &function : module.functions) prototype(function);
        out_ << "\n";

        for (const Module &module : modules_)
            for (const Function &function : module.functions) generate_function(function);

        out_ << "int main(int argc, char **argv) {\n"
                "    pp_runtime_init(argc, argv);\n";
        const Function *entry = functions_.at("main");
        if (entry->result == Type::Int)
            out_ << "    return (int)" << c_function_symbol("main") << "();\n";
        else {
            out_ << "    " << c_function_symbol("main") << "();\n"
                    "    return 0;\n";
        }
        out_ << "}\n";
        return out_.str();
    }

  private:
    struct Variable {
        Type type;
        std::string c_name;
    };

    const std::vector<Module> &modules_;
    std::unordered_map<std::string, const BuiltinSpec *> builtin_specs_;
    std::unordered_map<std::string, std::string> builtin_symbols_;
    std::unordered_map<std::string, const Shape *> shapes_;
    std::unordered_map<std::string, const Function *> functions_;
    std::unordered_set<std::string> emitted_shapes_;
    std::unordered_set<std::string> emitting_shapes_;
    std::vector<std::unordered_map<std::string, Variable>> scopes_;
    std::ostringstream out_;
    std::size_t unique_ = 0;
    int indent_ = 0;
    Type current_result_ = Type::Void;

    [[noreturn]] static void internal(const std::string &message) {
        throw Error("internal error: C backend: " + message);
    }

    static std::string sanitize(std::string value) {
        std::string out;
        out.reserve(value.size() + 8);
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '_') out += static_cast<char>(c);
            else out += '_';
        }
        return out;
    }

    static std::string c_record_tag(const std::string &name) { return "pp_type_" + sanitize(name); }
    static std::string c_function_symbol(const std::string &name) { return "pp_fn_" + sanitize(name); }
    static std::string c_async_impl_symbol(const std::string &name) { return "pp_async_impl_" + sanitize(name); }
    static std::string c_async_thunk_symbol(const std::string &name) { return "pp_async_thunk_" + sanitize(name); }
    static std::string c_async_context_tag(const std::string &name) { return "pp_async_ctx_" + sanitize(name); }

    bool is_shape(Type type) const { return shapes_.count(type.name) != 0; }
    bool is_object(Type type) const {
        auto found = shapes_.find(type.name);
        return found != shapes_.end() && found->second->reference_type;
    }

    std::string c_type(Type type) const {
        if (type == Type::Void) return "void";
        if (type == Type::Int) return "int64_t";
        if (type == Type::Float) return "double";
        if (type == Type::Bool) return "bool";
        if (type == Type::Str) return "const char *";
        if (type == Type::Nums) return "pp_numbers *";
        if (is_task_type(type)) return "pp_task *";
        if (is_pointer_like_type(type)) return c_type(pointee_type(type)) + " *";
        if (auto found = shapes_.find(type.name); found != shapes_.end()) {
            const std::string tag = c_record_tag(type.name);
            return found->second->reference_type ? tag + " *" : tag;
        }
        internal("unresolved type '" + type.name + "'");
    }

    void line(const std::string &text = {}) {
        out_ << std::string(static_cast<std::size_t>(indent_) * 4, ' ') << text << "\n";
    }

    void source_location(const Token &token) {
        std::string path;
        for (char c : token.file.lexically_normal().string()) {
            if (c == '\\' || c == '"') path += '\\';
            path += c;
        }
        out_ << "#line " << token.line << " \"" << path << "\"\n";
    }

    static std::string c_string(const std::string &value) {
        std::string result = "\"";
        for (unsigned char c : value) {
            if (c == '\\') result += "\\\\";
            else if (c == '"') result += "\\\"";
            else if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else if (c == '\t') result += "\\t";
            else if (c >= 32 && c < 127) result += static_cast<char>(c);
            else {
                static const char hex[] = "0123456789abcdef";
                result += "\\x";
                result += hex[c >> 4];
                result += hex[c & 15];
                result += "\"\"";
            }
        }
        return result + "\"";
    }

    void generate_shape(const Shape &shape) {
        if (emitted_shapes_.count(shape.name)) return;
        if (!emitting_shapes_.insert(shape.name).second)
            internal("recursive by-value type reached codegen despite semantic validation: " + shape.name);

        // Value fields need complete definitions first. Object/reference/pointer
        // fields only require the forward declaration above.
        for (const Parameter &field : shape.fields) {
            Type dependency = field.type;
            if (is_pointer_like_type(dependency)) continue;
            auto nested = shapes_.find(dependency.name);
            if (nested != shapes_.end() && !nested->second->reference_type) generate_shape(*nested->second);
        }

        out_ << "struct " << c_record_tag(shape.name) << " {\n";
        ++indent_;
        if (shape.fields.empty()) line("unsigned char pp_empty;");
        for (const Parameter &field : shape.fields)
            line(c_type(field.type) + " pp_f_" + sanitize(field.name) + ";");
        --indent_;
        out_ << "};\n\n";
        emitting_shapes_.erase(shape.name);
        emitted_shapes_.insert(shape.name);
    }

    void emit_parameter_list(const Function &function) {
        if (function.parameters.empty()) out_ << "void";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out_ << ", ";
            out_ << c_type(function.parameters[i].type) << " pp_v_" << sanitize(function.parameters[i].name);
        }
    }

    void prototype(const Function &function) {
        if (function.is_extern_native) {
            out_ << "extern " << c_type(function.result) << " " << function.native_symbol << "(";
            emit_parameter_list(function);
            out_ << ");\n";
            return;
        }
        if (function.is_async) {
            out_ << "struct " << c_async_context_tag(function.name) << " {\n";
            if (function.parameters.empty()) out_ << "    unsigned char pp_unused;\n";
            for (const Parameter &parameter : function.parameters)
                out_ << "    " << c_type(parameter.type) << " pp_a_" << sanitize(parameter.name) << ";\n";
            out_ << "};\n";
            out_ << "static " << c_type(function.result) << " " << c_async_impl_symbol(function.name) << "(";
            emit_parameter_list(function);
            out_ << ");\n";
            out_ << "static uintptr_t " << c_async_thunk_symbol(function.name) << "(void *pp_raw);\n";
            out_ << "pp_task *" << c_function_symbol(function.name) << "(";
            emit_parameter_list(function);
            out_ << ");\n";
            return;
        }
        out_ << c_type(function.result) << " " << c_function_symbol(function.name) << "(";
        emit_parameter_list(function);
        out_ << ");\n";
    }

    void generate_function_body(const Function &function, const std::string &symbol) {
        current_result_ = function.result;
        scopes_.clear();
        scopes_.push_back({});

        out_ << c_type(function.result) << " " << symbol << "(";
        if (function.parameters.empty()) out_ << "void";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out_ << ", ";
            const Parameter &parameter = function.parameters[i];
            const std::string name = "pp_v_" + sanitize(parameter.name);
            out_ << c_type(parameter.type) << " " << name;
            scopes_.back()[parameter.name] = {parameter.type, name};
        }
        out_ << ") {\n";
        ++indent_;
        for (const Stmt &statement : function.body) generate_statement(statement);
        if (function.result == Type::Void) line("return;");
        else if (function.name == "main") line("return 0;");
        --indent_;
        out_ << "}\n\n";
    }

    void generate_async_function(const Function &function) {
        generate_function_body(function, c_async_impl_symbol(function.name));

        out_ << "static uintptr_t " << c_async_thunk_symbol(function.name) << "(void *pp_raw) {\n";
        ++indent_;
        if (!function.parameters.empty())
            line("struct " + c_async_context_tag(function.name) + " *pp_ctx = (struct " +
                 c_async_context_tag(function.name) + " *)pp_raw;");
        std::string call = c_async_impl_symbol(function.name) + "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) call += ", ";
            call += "pp_ctx->pp_a_" + sanitize(function.parameters[i].name);
        }
        call += ")";
        if (function.result == Type::Void) {
            line(call + ";");
            line("return (uintptr_t)0;");
        } else if (function.result == Type::Float) {
            line("double pp_value = " + call + ";");
            line("uint64_t pp_bits = 0;");
            line("memcpy(&pp_bits, &pp_value, sizeof(pp_bits));");
            line("return (uintptr_t)pp_bits;");
        } else if (function.result == Type::Int || function.result == Type::Bool) {
            line(c_type(function.result) + " pp_value = " + call + ";");
            line("return (uintptr_t)(uint64_t)pp_value;");
        } else {
            line(c_type(function.result) + " pp_value = " + call + ";");
            line("return (uintptr_t)(const void *)pp_value;");
        }
        --indent_;
        out_ << "}\n\n";

        out_ << "pp_task *" << c_function_symbol(function.name) << "(";
        emit_parameter_list(function);
        out_ << ") {\n";
        ++indent_;
        if (function.parameters.empty()) {
            line("return pp_task_spawn(" + c_async_thunk_symbol(function.name) + ", NULL, 0);");
        } else {
            line("struct " + c_async_context_tag(function.name) + " pp_context;");
            for (const Parameter &parameter : function.parameters)
                line("pp_context.pp_a_" + sanitize(parameter.name) + " = pp_v_" + sanitize(parameter.name) + ";");
            line("return pp_task_spawn(" + c_async_thunk_symbol(function.name) +
                 ", &pp_context, (int64_t)sizeof(pp_context));");
        }
        --indent_;
        out_ << "}\n\n";
    }

    void generate_function(const Function &function) {
        if (function.is_extern_native) return;
        if (function.is_async) generate_async_function(function);
        else generate_function_body(function, c_function_symbol(function.name));
    }

    const Variable *lookup(const std::string &name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    Generated freeze(Generated value) {
        if (value.type == Type::Void) internal("attempted to materialize void expression");
        const std::string name = "pp_tmp_" + std::to_string(unique_++);
        line(c_type(value.type) + " " + name + " = " + value.code + ";");
        return {value.type, name};
    }

    void generate_block(const std::vector<Stmt> &statements) {
        scopes_.push_back({});
        ++indent_;
        for (const Stmt &statement : statements) generate_statement(statement);
        --indent_;
        scopes_.pop_back();
    }

    static bool is_addressable(const Expr &expression) {
        if (expression.kind == Expr::Kind::Variable || expression.kind == Expr::Kind::Member) return true;
        return expression.kind == Expr::Kind::Unary && expression.value == "*";
    }

    std::string lvalue(const Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            const Variable *variable = lookup(expression.value);
            if (!variable) internal("unknown lvalue '" + expression.value + "'");
            return variable->c_name;
        }
        if (expression.kind == Expr::Kind::Member) {
            Generated base = generate_expression(*expression.children[0]);
            const bool pointer_access = is_object(base.type) || is_reference_type(base.type);
            return "(" + base.code + ")" + (pointer_access ? "->" : ".") + "pp_f_" + sanitize(expression.value);
        }
        if (expression.kind == Expr::Kind::Unary && expression.value == "*") {
            Generated pointer = generate_expression(*expression.children[0]);
            return "(*(" + pointer.code + "))";
        }
        internal("unsupported lvalue expression");
    }

    Generated member(const Expr &expression) {
        Generated base = generate_expression(*expression.children[0]);
        return {expression.inferred_type,
                "(" + base.code + ")" + ((is_object(base.type) || is_reference_type(base.type)) ? "->" : ".") +
                    "pp_f_" + sanitize(expression.value)};
    }

    void generate_statement(const Stmt &statement) {
        source_location(statement.token);
        switch (statement.kind) {
            case Stmt::Kind::Variable: {
                Generated value = generate_expression(*statement.expression);
                const Type type = statement.declared_type == Type::Infer ? value.type : statement.declared_type;
                const std::string name = "pp_local_" + std::to_string(unique_++);
                line(c_type(type) + " " + name + " = " + value.code + ";");
                scopes_.back()[statement.name] = {type, name};
                break;
            }
            case Stmt::Kind::Assign: {
                if (statement.target->kind == Expr::Kind::Index) {
                    Generated base = freeze(generate_expression(*statement.target->children[0]));
                    Generated index = freeze(generate_expression(*statement.target->children[1]));
                    Generated value = generate_expression(*statement.expression);
                    if (statement.assignment_op == "=" || statement.assignment_op == "<-")
                        line("pp_put(" + base.code + ", " + index.code + ", " + value.code + ");");
                    else {
                        std::string op = statement.assignment_op.substr(0, 1);
                        const std::unordered_map<std::string, std::string> checked = {
                            {"+", "pp_add_i64"}, {"-", "pp_sub_i64"}, {"*", "pp_mul_i64"}, {"/", "pp_div_i64"}};
                        line("pp_put(" + base.code + ", " + index.code + ", " + checked.at(op) +
                             "(pp_at(" + base.code + ", " + index.code + "), " + value.code + "));" );
                    }
                    break;
                }
                const std::string target = lvalue(*statement.target);
                Generated value = generate_expression(*statement.expression);
                if (statement.assignment_op == "=" || statement.assignment_op == "<-") {
                    line(target + " = " + value.code + ";");
                } else {
                    const std::string op = statement.assignment_op.substr(0, 1);
                    if (statement.target->inferred_type == Type::Int) {
                        const std::unordered_map<std::string, std::string> checked = {
                            {"+", "pp_add_i64"}, {"-", "pp_sub_i64"}, {"*", "pp_mul_i64"}, {"/", "pp_div_i64"}};
                        line(target + " = " + checked.at(op) + "(" + target + ", " + value.code + ");");
                    } else line(target + " " + statement.assignment_op + " " + value.code + ";");
                }
                break;
            }
            case Stmt::Kind::Expression: {
                Generated value = generate_expression(*statement.expression);
                if (value.type == Type::Void) line(value.code + ";");
                else line("(void)(" + value.code + ");");
                break;
            }
            case Stmt::Kind::Say: {
                Generated value = generate_expression(*statement.expression);
                if (value.type == Type::Int) line("pp_println_int(" + value.code + ");");
                else if (value.type == Type::Float) line("pp_println_float(" + value.code + ");");
                else if (value.type == Type::Bool) line("pp_println_bool(" + value.code + ");");
                else if (value.type == Type::Str) line("pp_println_str(" + value.code + ");");
                else internal("non-scalar say reached C backend");
                break;
            }
            case Stmt::Kind::Return:
                if (!statement.expression) line("return;");
                else line("return " + generate_expression(*statement.expression).code + ";");
                break;
            case Stmt::Kind::If: {
                Generated condition = generate_expression(*statement.expression);
                line("if (" + condition.code + ") {");
                generate_block(statement.body);
                line("}");
                if (!statement.alternative.empty()) {
                    line("else {");
                    generate_block(statement.alternative);
                    line("}");
                }
                break;
            }
            case Stmt::Kind::While: {
                // Re-generate the source expression on every iteration. Any
                // helper temporaries emitted by the expression sit in the loop
                // body, preserving side effects and short-circuit semantics.
                line("for (;;) {");
                scopes_.push_back({});
                ++indent_;
                Generated condition = generate_expression(*statement.expression);
                line("if (!(" + condition.code + ")) break;");
                for (const Stmt &child : statement.body) generate_statement(child);
                --indent_;
                scopes_.pop_back();
                line("}");
                break;
            }
            case Stmt::Kind::Each: {
                Generated start = freeze(generate_expression(*statement.expression));
                Generated end = freeze(generate_expression(*statement.upper));
                const std::string name = "pp_range_" + std::to_string(unique_++);
                line("for (int64_t " + name + " = " + start.code + "; " + name + " < " + end.code + "; ++" + name + ") {");
                scopes_.push_back({{statement.name, {Type::Int, name}}});
                ++indent_;
                for (const Stmt &child : statement.body) generate_statement(child);
                --indent_;
                scopes_.pop_back();
                line("}");
                break;
            }
            case Stmt::Kind::Break: line("break;"); break;
            case Stmt::Kind::Continue: line("continue;"); break;
            case Stmt::Kind::Unsafe:
                line("{");
                generate_block(statement.body);
                line("}");
                break;
        }
    }

    Generated generate_expression(const Expr &expression) {
        switch (expression.kind) {
            case Expr::Kind::Integer:
                if (expression.value == "9223372036854775808") return {Type::Int, "INT64_C(9223372036854775807)"};
                return {Type::Int, "INT64_C(" + expression.value + ")"};
            case Expr::Kind::Float: return {Type::Float, expression.value};
            case Expr::Kind::String: return {Type::Str, c_string(expression.value)};
            case Expr::Kind::Boolean: return {Type::Bool, expression.value == "yes" ? "true" : "false"};
            case Expr::Kind::Variable: {
                const Variable *variable = lookup(expression.value);
                if (!variable) internal("unknown variable '" + expression.value + "'");
                return {variable->type, variable->c_name};
            }
            case Expr::Kind::Member: return member(expression);
            case Expr::Kind::Index: {
                Generated base = freeze(generate_expression(*expression.children[0]));
                Generated index = freeze(generate_expression(*expression.children[1]));
                return freeze({Type::Int, "pp_at(" + base.code + ", " + index.code + ")"});
            }
            case Expr::Kind::List: {
                Generated list = freeze({Type::Nums, "pp_numbers_new()"});
                for (const auto &child : expression.children) {
                    Generated item = generate_expression(*child);
                    line("pp_push(" + list.code + ", " + item.code + ");");
                }
                return list;
            }
            case Expr::Kind::Call:
            case Expr::Kind::MethodCall: return generate_call(expression);
            case Expr::Kind::SizeOf:
                return {Type::Int, "(int64_t)sizeof(" + c_type(Type{expression.value}) + ")"};
            case Expr::Kind::AlignOf:
                return {Type::Int, "(int64_t)_Alignof(" + c_type(Type{expression.value}) + ")"};
            case Expr::Kind::Unary: return generate_unary(expression);
            case Expr::Kind::Binary: return generate_binary(expression);
        }
        internal("unknown expression kind");
    }

    Generated generate_unary(const Expr &expression) {
        if (expression.value == "await") {
            Generated task = freeze(generate_expression(*expression.children[0]));
            const Type result = expression.inferred_type;
            if (result == Type::Void) return {Type::Void, "pp_task_await_void(" + task.code + ")"};
            if (result == Type::Float) return freeze({Type::Float, "pp_task_await_f64(" + task.code + ")"});
            if (result == Type::Int) return freeze({Type::Int, "pp_task_await_i64(" + task.code + ")"});
            if (result == Type::Bool) return freeze({Type::Bool, "(bool)pp_task_await_i64(" + task.code + ")"});
            return freeze({result, "(" + c_type(result) + ")pp_task_await_ptr(" + task.code + ")"});
        }
        if (expression.value == "-" && expression.children[0]->kind == Expr::Kind::Integer &&
            expression.children[0]->value == "9223372036854775808") return {Type::Int, "INT64_MIN"};
        if (expression.value == "&" || expression.value == "&mut" || expression.value == "&raw")
            return {expression.inferred_type, "&(" + lvalue(*expression.children[0]) + ")"};

        Generated operand = generate_expression(*expression.children[0]);
        if (expression.value == "*") return {expression.inferred_type, "(*(" + operand.code + "))"};
        if (expression.value == "not") return {Type::Bool, "(!(" + operand.code + "))"};
        if (expression.value == "~") return {Type::Int, "(~(" + operand.code + "))"};
        if (operand.type == Type::Int) return freeze({Type::Int, "pp_neg_i64(" + operand.code + ")"});
        if (operand.type == Type::Float) return {Type::Float, "(-(" + operand.code + "))"};
        internal("invalid unary expression after semantic analysis");
    }

    Generated generate_binary(const Expr &expression) {
        const std::string &op = expression.value;
        if (op == "and" || op == "or") {
            Generated left = freeze(generate_expression(*expression.children[0]));
            Generated result = freeze(left);
            line("if (" + std::string(op == "or" ? "!(" : "(") + result.code + ")) {");
            ++indent_;
            Generated right = generate_expression(*expression.children[1]);
            line(result.code + " = " + right.code + ";");
            --indent_;
            line("}");
            return result;
        }

        Generated left = freeze(generate_expression(*expression.children[0]));
        Generated right = freeze(generate_expression(*expression.children[1]));
        const Type type = left.type;

        if (is_raw_pointer_type(type) && right.type == Type::Int && (op == "+" || op == "-"))
            return {type, "(" + left.code + " " + op + " " + right.code + ")"};

        if (op == "==" || op == "!=") {
            std::string code;
            if (type == Type::Str) code = "pp_str_eq(" + left.code + ", " + right.code + ")";
            else code = "(" + left.code + " == " + right.code + ")";
            if (op == "!=") code = "(!" + code + ")";
            return {Type::Bool, code};
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=")
            return {Type::Bool, "(" + left.code + " " + op + " " + right.code + ")"};

        if (type == Type::Int) {
            const std::unordered_map<std::string, std::string> checked = {
                {"+", "pp_add_i64"}, {"-", "pp_sub_i64"}, {"*", "pp_mul_i64"},
                {"/", "pp_div_i64"}, {"%", "pp_mod_i64"}};
            if (auto found = checked.find(op); found != checked.end())
                return freeze({Type::Int, found->second + "(" + left.code + ", " + right.code + ")"});
            if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>")
                return {Type::Int, "(" + left.code + " " + op + " " + right.code + ")"};
        }
        if (type == Type::Str && op == "+")
            return freeze({Type::Str, "pp_concat(" + left.code + ", " + right.code + ")"});
        if (type == Type::Float && (op == "+" || op == "-" || op == "*" || op == "/"))
            return {Type::Float, "(" + left.code + " " + op + " " + right.code + ")"};
        internal("invalid binary expression after semantic analysis: " + op);
    }

    Generated generate_call(const Expr &expression) {
        if (expression.value == "move") {
            Generated value = generate_expression(*expression.children.at(0));
            return freeze(value);
        }
        if (expression.value == "drop") {
            Generated value = freeze(generate_expression(*expression.children.at(0)));
            if (value.type == Type::Nums)
                return {Type::Void, "pp_numbers_free(" + value.code + ")"};
            return {Type::Void, "pp_object_free((void *)(" + value.code + "))"};
        }
        // Type construction is distinct for value structs and identity objects.
        if (auto shape_it = shapes_.find(expression.value); shape_it != shapes_.end()) {
            const Shape &shape = *shape_it->second;
            if (!shape.reference_type) {
                std::string code = "((" + c_record_tag(shape.name) + "){";
                for (std::size_t i = 0; i < expression.children.size(); ++i) {
                    Generated value = freeze(generate_expression(*expression.children[i]));
                    if (i) code += ", ";
                    code += ".pp_f_" + sanitize(shape.fields[i].name) + " = " + value.code;
                }
                if (shape.fields.empty()) code += ".pp_empty = 0";
                return freeze({Type{shape.name}, code + "})"});
            }

            const std::string temp = "pp_obj_" + std::to_string(unique_++);
            line(c_record_tag(shape.name) + " *" + temp + " = (" + c_record_tag(shape.name) +
                 " *)pp_object_alloc(sizeof(" + c_record_tag(shape.name) + "));" );
            if (!shape.initializer_name.empty()) {
                std::string init = c_function_symbol(shape.initializer_name) + "(" + temp;
                for (const auto &child : expression.children) {
                    Generated value = freeze(generate_expression(*child));
                    init += ", " + value.code;
                }
                init += ")";
                line(init + ";");
            }
            return {Type{shape.name}, temp};
        }

        auto function = functions_.find(expression.value);
        auto builtin = builtin_specs_.find(expression.value);
        if (function == functions_.end() && builtin == builtin_specs_.end())
            internal("unknown call target '" + expression.value + "'");

        std::vector<Generated> arguments;
        arguments.reserve(expression.children.size());
        for (std::size_t i = 0; i < expression.children.size(); ++i) {
            Generated actual = generate_expression(*expression.children[i]);
            if (function != functions_.end() && i < function->second->parameters.size()) {
                const Type expected = function->second->parameters[i].type;
                if (is_reference_type(expected) && is_shape(actual.type) && !is_object(actual.type)) {
                    if (i == 0 && expression.kind == Expr::Kind::MethodCall && is_addressable(*expression.children[i]))
                        actual = {expected, "&(" + lvalue(*expression.children[i]) + ")"};
                    else {
                        actual = freeze(actual);
                        actual = {expected, "&(" + actual.code + ")"};
                    }
                }
            }
            arguments.push_back(freeze(actual));
        }

        if (expression.value == "print" || expression.value == "println") {
            const std::string suffix = type_name(arguments.at(0).type);
            return {Type::Void, "pp_" + expression.value + "_" + suffix + "(" + arguments[0].code + ")"};
        }

        const Type result = function != functions_.end()
            ? (function->second->is_async ? task_type(function->second->result) : function->second->result)
            : builtin->second->result;
        std::string callee;
        if (function != functions_.end())
            callee = function->second->is_extern_native ? function->second->native_symbol : c_function_symbol(expression.value);
        else if (auto symbol = builtin_symbols_.find(expression.value); symbol != builtin_symbols_.end()) callee = symbol->second;
        else internal("builtin without lowering: " + expression.value);

        std::string code = callee + "(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i) code += ", ";
            code += arguments[i].code;
        }
        code += ")";
        return result == Type::Void ? Generated{result, code} : freeze({result, code});
    }
};

#endif  // PUNPUN_BACKEND_C_HPP
