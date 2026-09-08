#ifndef PUNPUN_SEMANTIC_HPP
#define PUNPUN_SEMANTIC_HPP

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend.hpp"
#include "builtins.hpp"

struct SemanticSignature {
    std::vector<Type> parameters;
    std::vector<std::string> parameter_names;
    std::vector<std::shared_ptr<Expr>> defaults;
    Type result;
    Token token;
    bool builtin = false;
    std::string owner_type;
    bool self_mutable = false;
    bool initializer = false;
    Visibility visibility = Visibility::Public;
    bool is_async = false;
};

struct SemanticVariable {
    Type type;
    bool mutable_value = false;
    Token token;
    bool parameter = false;
    std::size_t scope_depth = 0;
    bool moved = false;
};

// One authoritative semantic pass for check/build/run/HIR/LSP worker use.
class SemanticAnalyzer {
  public:
    explicit SemanticAnalyzer(std::vector<Module> &modules) : modules_(modules) {}

    void analyze() {
        collect_builtins();
        collect_contracts();
        collect_shapes();
        collect_functions();
        validate_contracts();
        validate_conformance();
        validate_injections();
        validate_entry();
        for (const auto &[name, shape] : shapes_) {
            (void)name;
            validate_shape(*shape);
        }
        for (Module &module : modules_)
            for (Function &function : module.functions) analyze_function(function);
    }

    const std::unordered_map<std::string, SemanticSignature> &signatures() const { return signatures_; }
    const std::unordered_map<std::string, const Shape *> &shapes() const { return shapes_; }

  private:
    std::vector<Module> &modules_;
    std::unordered_map<std::string, SemanticSignature> signatures_;
    std::unordered_map<std::string, const Shape *> shapes_;
    std::unordered_map<std::string, const Contract *> contracts_;
    std::unordered_map<std::string, int> shape_state_;
    std::vector<std::unordered_map<std::string, SemanticVariable>> scopes_;
    Type current_result_ = Type::Void;
    std::string current_owner_;
    const Function *current_function_ = nullptr;
    int loop_depth_ = 0;
    int unsafe_depth_ = 0;

    [[noreturn]] static void fail(const Token &token, const std::string &message,
                                  const std::string &help = {}, const std::string &code = "E1000") {
        throw Error(ppdiag::format(token.file, token.line, token.column, token.length,
                                   code, message, message, help));
    }

    static std::size_t edit_distance(const std::string &a, const std::string &b) {
        std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);
        for (std::size_t j = 0; j <= b.size(); ++j) previous[j] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            current[0] = i;
            for (std::size_t j = 1; j <= b.size(); ++j) {
                const std::size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
                current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
            }
            previous.swap(current);
        }
        return previous[b.size()];
    }

    static std::string closest_name(const std::string &needle, const std::vector<std::string> &candidates) {
        std::string best;
        std::size_t best_distance = std::numeric_limits<std::size_t>::max();
        for (const std::string &candidate : candidates) {
            if (candidate == needle) continue;
            const std::size_t distance = edit_distance(needle, candidate);
            if (distance < best_distance || (distance == best_distance && candidate < best)) {
                best = candidate; best_distance = distance;
            }
        }
        const std::size_t threshold = std::max<std::size_t>(1, std::min<std::size_t>(3, needle.size() / 3 + 1));
        return best_distance <= threshold ? best : std::string{};
    }

    std::string name_help(const std::string &name) const {
        std::vector<std::string> candidates;
        for (const auto &scope : scopes_) for (const auto &[candidate, variable] : scope) { (void)variable; candidates.push_back(candidate); }
        for (const auto &[candidate, signature] : signatures_) { (void)signature; candidates.push_back(candidate); }
        for (const auto &[candidate, shape] : shapes_) { (void)shape; candidates.push_back(candidate); }
        const std::string closest = closest_name(name, candidates);
        return closest.empty() ? std::string{} : "did you mean `" + closest + "`?";
    }

    static Token builtin_token() { return {TokenKind::Word, "builtin", "<builtin>", 1, 1, 0, 7}; }

    void add_builtin(const std::string &name, std::vector<Type> parameters, Type result) {
        std::vector<std::string> parameter_names;
        for (std::size_t i = 0; i < parameters.size(); ++i) parameter_names.push_back("arg" + std::to_string(i + 1));
        std::vector<std::shared_ptr<Expr>> defaults(parameters.size());
        signatures_.emplace(name, SemanticSignature{std::move(parameters), std::move(parameter_names), std::move(defaults), result, builtin_token(), true,
                                                   "", false, false, Visibility::Public});
    }

    void collect_builtins() {
        for (const BuiltinSpec &builtin : punpun_builtins())
            add_builtin(builtin.name, builtin.parameters, builtin.result);
    }

    void collect_contracts() {
        for (const Module &module : modules_) for (const Contract &contract : module.contracts) {
            if (contracts_.count(contract.name)) fail(contract.token, "duplicate contract '" + contract.name + "'", {}, "E0202");
            contracts_[contract.name] = &contract;
        }
    }

    void collect_shapes() {
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) {
                if (shapes_.count(shape.name) || contracts_.count(shape.name) || signatures_.count(shape.name))
                    fail(shape.token, "duplicate type/contract or built-in name '" + shape.name + "'");
                shapes_[shape.name] = &shape;
            }
        }
    }

    void collect_functions() {
        for (const Module &module : modules_) {
            for (const Function &function : module.functions) {
                if (signatures_.count(function.name) || (!function.is_method && shapes_.count(function.name)))
                    fail(function.token, "duplicate function '" + function.name + "'");
                validate_type(function.result, function.token, true);
                SemanticSignature signature{{}, {}, {}, function.result, function.token, false,
                                            function.owner_type, function.self_mutable,
                                            function.is_initializer, function.visibility};
                signature.is_async = function.is_async;
                if (function.is_async) {
                    if (function.is_initializer)
                        fail(function.token, "constructors cannot be async", {}, "E1500");
                    if (function.name == "main")
                        fail(function.token, "the program entry function cannot be async",
                             "use launch { await work(); } as the root executor", "E1500");
                    if (auto result_shape = shapes_.find(function.result.name);
                        result_shape != shapes_.end() && !result_shape->second->reference_type)
                        fail(function.token, "async functions cannot yet return by-value structs",
                             "return a scalar, object/reference value, or void in this beta", "E1501");
                }
                for (const Parameter &parameter : function.parameters) {
                    validate_type(parameter.type, parameter.token);
                    if (function.is_async && (is_pointer_like_type(parameter.type) || parameter.type == Type::Nums || is_object(parameter.type)))
                        fail(parameter.token, "async parameters must be independently owned values in this beta",
                             "references, raw pointers, object identities, and nums handles need Send/ownership analysis before crossing task boundaries", "E1502");
                    signature.parameters.push_back(parameter.type);
                    signature.parameter_names.push_back(parameter.name);
                    signature.defaults.push_back(parameter.default_value);
                }
                signatures_[function.name] = std::move(signature);
            }
        }
    }

    void validate_contracts() const {
        for (const auto &[name, contract] : contracts_) {
            (void)name;
            for (const ContractMethod &method : contract->methods) {
                validate_type(method.result, method.token, true);
                for (const Parameter &parameter : method.parameters) validate_type(parameter.type, parameter.token);
            }
        }
    }

    void validate_conformance() const {
        for (const auto &[shape_name, shape] : shapes_) {
            for (const std::string &contract_name : shape->contracts) {
                auto contract_it = contracts_.find(contract_name);
                if (contract_it == contracts_.end())
                    fail(shape->token, "unknown contract '" + contract_name + "'", {}, "E0201");
                for (const ContractMethod &required : contract_it->second->methods) {
                    const std::string lowered = shape_name + "::" + required.name;
                    auto actual_it = signatures_.find(lowered);
                    if (actual_it == signatures_.end())
                        fail(shape->token, "object/type '" + shape_name + "' does not implement contract method '" + contract_name + "." + required.name + "'",
                             "add fn " + required.name + "(...) to " + shape_name, "E0403");
                    const SemanticSignature &actual = actual_it->second;
                    const std::size_t actual_user_count = actual.parameters.empty() ? 0 : actual.parameters.size() - 1;
                    if (actual_user_count != required.parameters.size() || actual.result != required.result)
                        fail(actual.token, "method '" + shape_name + "." + required.name + "' does not match contract '" + contract_name + "'", {}, "E0403");
                    for (std::size_t i = 0; i < required.parameters.size(); ++i)
                        if (actual.parameters[i + 1] != required.parameters[i].type)
                            fail(actual.token, "method '" + shape_name + "." + required.name + "' has incompatible contract parameter type", {}, "E0403");
                }
            }
        }
    }

    void validate_injections() const {
        for (const Module &module : modules_) for (const ForeignInjection &injection : module.injections) {
            static const std::unordered_set<std::string> supported{"c", "cpp", "cxx", "rust", "asm"};
            if (!supported.count(injection.language))
                fail(injection.token, "unknown or unsupported injection language '" + injection.language + "'",
                     "supported adapters are c, cpp/cxx, rust, and asm", "E0601");
        }
        for (const auto &[name, signature] : signatures_) {
            (void)name;
            if (signature.builtin) continue;
            // The source Function carries the extern flag; locate it only when
            // needed so ordinary PunPun programs pay no injection startup tax.
        }
        for (const Module &module : modules_) for (const Function &function : module.functions) if (function.is_extern_native) {
            if (function.parameters.size() > 6)
                fail(function.token, "extern native function currently supports at most six scalar parameters on the direct x86-64 backend", {}, "E0604");
            auto abi_ok = [&](Type type) {
                return type == Type::Void || type == Type::Int || type == Type::Bool || type == Type::Str || is_raw_pointer_type(type);
            };
            if (!abi_ok(function.result)) fail(function.token, "extern native result type is not C-ABI-safe yet", {}, "E0604");
            for (const Parameter &parameter : function.parameters)
                if (!abi_ok(parameter.type) || parameter.type == Type::Void)
                    fail(parameter.token, "extern native parameter type is not C-ABI-safe yet", {}, "E0604");
        }
    }

    void validate_entry() const {
        auto main = signatures_.find("main");
        if (main == signatures_.end())
            throw Error("error[E1001]: program does not define `fn main()` (legacy `launch:` is also accepted during 0.5 migration)");
        if (!main->second.parameters.empty() ||
            (main->second.result != Type::Void && main->second.result != Type::Int))
            fail(main->second.token, "invalid main signature; expected fn main() or fn main() -> i64");
    }

    void validate_type(Type type, const Token &token, bool allow_void = false) const {
        if (is_task_type(type)) {
            validate_type(task_result_type(type), token, true);
            return;
        }
        if (is_pointer_like_type(type)) {
            const Type inner = pointee_type(type);
            if (inner == Type::Infer || inner == Type::Void) fail(token, "invalid pointer/reference type '" + type.name + "'");
            validate_type(inner, token, false);
            return;
        }
        if (type == Type::Int || type == Type::Float || type == Type::Bool || type == Type::Str ||
            type == Type::Nums || shapes_.count(type.name) || (allow_void && type == Type::Void)) return;
        fail(token, "unknown or invalid value type '" + type.name + "'");
    }

    bool is_object(Type type) const {
        auto found = shapes_.find(type.name);
        return found != shapes_.end() && found->second->reference_type;
    }

    void validate_shape(const Shape &shape) {
        if (shape_state_[shape.name] == 2) return;
        if (shape_state_[shape.name] == 1) fail(shape.token, "recursive by-value struct '" + shape.name + "'");
        shape_state_[shape.name] = 1;
        std::unordered_set<std::string> fields;
        for (const Parameter &field : shape.fields) {
            validate_type(field.type, field.token);
            if (!fields.insert(field.name).second) fail(field.token, "duplicate field '" + field.name + "'");
            // Reference/object/pointer edges do not make recursive value layout.
            if (!is_pointer_like_type(field.type)) {
                auto nested = shapes_.find(field.type.name);
                if (nested != shapes_.end() && !nested->second->reference_type) validate_shape(*nested->second);
            }
        }
        if (shape.reference_type && !shape.fields.empty() && shape.initializer_name.empty())
            fail(shape.token, "object '" + shape.name + "' has fields but no init constructor",
                 "add init(...) { ... } and initialize every field");
        shape_state_[shape.name] = 2;
    }

    static bool block_returns(const std::vector<Stmt> &statements) {
        for (const Stmt &statement : statements) {
            if (statement.kind == Stmt::Kind::Return) return true;
            if (statement.kind == Stmt::Kind::If && !statement.alternative.empty() &&
                block_returns(statement.body) && block_returns(statement.alternative)) return true;
        }
        return false;
    }

    void analyze_function(Function &function) {
        if (function.is_extern_native) return;
        if (function.name != "main" && !function.is_initializer && function.result != Type::Void &&
            !block_returns(function.body))
            fail(function.token, "function '" + function.source_name + "' may exit without returning " + type_name(function.result));

        current_function_ = &function;
        current_owner_ = function.owner_type;
        current_result_ = function.result;
        loop_depth_ = 0;
        unsafe_depth_ = 0;
        scopes_.clear();
        scopes_.push_back({});
        for (const Parameter &parameter : function.parameters) {
            if (scopes_.back().count(parameter.name)) fail(parameter.token, "duplicate parameter '" + parameter.name + "'");
            scopes_.back()[parameter.name] = {parameter.type, parameter.mutable_value, parameter.token, true, 0};
        }
        for (const Parameter &parameter : function.parameters) {
            if (!parameter.default_value) continue;
            const Type value = expression_type(*parameter.default_value);
            require(value, parameter.type, parameter.default_value->token, "default argument");
        }
        for (Stmt &statement : function.body) analyze_statement(statement);
        if (function.is_initializer) validate_initializer(function);
        current_function_ = nullptr;
    }

    void validate_initializer(const Function &function) const {
        const Shape &shape = *shapes_.at(function.owner_type);
        std::unordered_set<std::string> assigned;
        collect_direct_self_assignments(function.body, assigned);
        for (const Parameter &field : shape.fields) {
            if (!assigned.count(field.name))
                fail(function.token, "constructor for '" + shape.name + "' does not initialize field '" + field.name + "'",
                     "assign self." + field.name + " before init returns", "E1101");
        }
    }

    static void collect_direct_self_assignments(const std::vector<Stmt> &statements,
                                                 std::unordered_set<std::string> &assigned) {
        for (const Stmt &statement : statements) {
            if (statement.kind == Stmt::Kind::Assign && statement.target &&
                statement.target->kind == Expr::Kind::Member && !statement.target->children.empty() &&
                statement.target->children[0]->kind == Expr::Kind::Variable &&
                statement.target->children[0]->value == "self") {
                assigned.insert(statement.target->value);
            }
            // Assignments nested under conditionals do not prove definite initialization.
            if (statement.kind == Stmt::Kind::Unsafe) collect_direct_self_assignments(statement.body, assigned);
        }
    }

    std::optional<SemanticVariable> lookup(const std::string &name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (auto found = scope->find(name); found != scope->end()) return found->second;
        }
        return std::nullopt;
    }

    SemanticVariable *lookup_mut(const std::string &name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (auto found = scope->find(name); found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    static void require(Type actual, Type expected, const Token &token, const std::string &context) {
        if (actual != expected)
            fail(token, context + " requires " + type_name(expected) + ", got " + type_name(actual),
                 "expected " + type_name(expected) + " but this expression has type " + type_name(actual), "E1200");
    }

    void analyze_block(std::vector<Stmt> &statements) {
        scopes_.push_back({});
        for (Stmt &statement : statements) analyze_statement(statement);
        scopes_.pop_back();
    }

    const Parameter &find_field(Type base, const Expr &expression) const {
        if (is_reference_type(base)) base = pointee_type(base);
        const auto shape = shapes_.find(base.name);
        if (shape == shapes_.end()) fail(expression.token, "field access requires a struct/object, got " + base.name);
        for (const Parameter &field : shape->second->fields) {
            if (field.name != expression.value) continue;
            if (field.visibility == Visibility::Private && current_owner_ != base.name)
                fail(expression.token, "field '" + base.name + "." + field.name + "' is private", {}, "E1301");
            if (field.visibility == Visibility::Protected && current_owner_ != base.name)
                fail(expression.token, "field '" + base.name + "." + field.name + "' is protected", {}, "E1302");
            return field;
        }
        std::vector<std::string> candidates;
        for (const Parameter &field : shape->second->fields) candidates.push_back(field.name);
        const std::string closest = closest_name(expression.value, candidates);
        fail(expression.token, "type '" + base.name + "' has no field '" + expression.value + "'",
             closest.empty() ? std::string{} : "did you mean `" + closest + "`?", "E0401");
    }

    Type member_type(Type base, const Expr &expression) const { return find_field(base, expression).type; }

    bool expression_is_mutable_lvalue(Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            const auto variable = lookup(expression.value);
            return variable && variable->mutable_value;
        }
        if (expression.kind == Expr::Kind::Member) return expression_is_mutable_lvalue(*expression.children[0]);
        if (expression.kind == Expr::Kind::Unary && expression.value == "*") {
            const Type pointer = expression_type(*expression.children[0]);
            return is_raw_pointer_type(pointer) || is_mut_reference_type(pointer);
        }
        return false;
    }

    Type assignment_target(Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            const auto variable = lookup(expression.value);
            if (!variable) fail(expression.token, "unknown binding '" + expression.value + "'");
            if (!variable->mutable_value)
                fail(expression.token, "cannot assign to immutable binding '" + expression.value + "'",
                     "declare it with `let mut " + expression.value + " = ...`", "E1400");
            expression.inferred_type = variable->type;
            return variable->type;
        }
        if (expression.kind == Expr::Kind::Member) {
            Type base_type = expression_type(*expression.children[0]);
            (void)find_field(base_type, expression);
            if (!expression_is_mutable_lvalue(*expression.children[0]))
                fail(expression.token, "cannot mutate field through an immutable value",
                     "use a mutable binding or mutable self", "E1401");
            const Type type = member_type(base_type, expression);
            expression.inferred_type = type;
            return type;
        }
        if (expression.kind == Expr::Kind::Unary && expression.value == "*") {
            const Type pointer = expression_type(*expression.children[0]);
            if (!is_pointer_like_type(pointer)) fail(expression.token, "dereference assignment requires pointer/reference");
            if (is_raw_pointer_type(pointer) && unsafe_depth_ == 0)
                fail(expression.token, "raw pointer dereference requires unsafe context",
                     "wrap the operation in unsafe { ... }", "E0801");
            if (is_reference_type(pointer) && !is_mut_reference_type(pointer))
                fail(expression.token, "cannot assign through immutable reference", {}, "E0802");
            expression.inferred_type = pointee_type(pointer);
            return expression.inferred_type;
        }
        fail(expression.token, "assignment needs a mutable binding, field, list index, or dereference");
    }

    void analyze_statement(Stmt &statement) {
        switch (statement.kind) {
            case Stmt::Kind::Variable: {
                if (scopes_.back().count(statement.name))
                    fail(statement.token, "duplicate variable '" + statement.name + "' in this scope");
                const Type value = expression_type(*statement.expression);
                const Type type = statement.declared_type == Type::Infer ? value : statement.declared_type;
                validate_type(type, statement.token);
                require(value, type, statement.expression->token, "initializer");
                scopes_.back()[statement.name] = {type, statement.mutable_value, statement.token, false, scopes_.size() - 1};
                break;
            }
            case Stmt::Kind::Assign: {
                if (statement.target->kind == Expr::Kind::Index) {
                    Expr &target = *statement.target;
                    const Type base = expression_type(*target.children[0]);
                    const Type index = expression_type(*target.children[1]);
                    const Type value = expression_type(*statement.expression);
                    require(base, Type::Nums, target.token, "indexed assignment");
                    require(index, Type::Int, target.children[1]->token, "list index");
                    require(value, Type::Int, statement.expression->token, "list element");
                    target.inferred_type = Type::Int;
                } else {
                    if (statement.target->kind == Expr::Kind::Variable &&
                        statement.assignment_op != "=" && statement.assignment_op != "<-") {
                        if (const SemanticVariable *variable = lookup_mut(statement.target->value); variable && variable->moved)
                            fail(statement.target->token, "use of moved value '" + statement.target->value + "'",
                                 "assign a new value before reading this binding", "E0703");
                    }
                    const Type target = assignment_target(*statement.target);
                    const Type value = expression_type(*statement.expression);
                    require(value, target, statement.expression->token, "assignment");
                    if (statement.assignment_op != "=" && statement.assignment_op != "<-") {
                        if (target != Type::Int && target != Type::Float)
                            fail(statement.token, "compound assignment requires numeric target");
                    }
                    if (statement.target->kind == Expr::Kind::Variable) {
                        SemanticVariable *variable = lookup_mut(statement.target->value);
                        if (variable) variable->moved = false;
                    }
                }
                break;
            }
            case Stmt::Kind::Expression:
                (void)expression_type(*statement.expression);
                break;
            case Stmt::Kind::Say: {
                const Type value = expression_type(*statement.expression);
                if (value != Type::Int && value != Type::Float && value != Type::Str && value != Type::Bool)
                    fail(statement.expression->token, "say requires int, float, bool, or str");
                break;
            }
            case Stmt::Kind::Return:
                if (!statement.expression) require(Type::Void, current_result_, statement.token, "return");
                else {
                    const Type value = expression_type(*statement.expression);
                    require(value, current_result_, statement.expression->token, "return");
                    if (is_reference_type(value) && statement.expression->kind == Expr::Kind::Unary &&
                        (statement.expression->value == "&" || statement.expression->value == "&mut") &&
                        statement.expression->children[0]->kind == Expr::Kind::Variable) {
                        auto variable = lookup(statement.expression->children[0]->value);
                        if (variable && !variable->parameter)
                            fail(statement.expression->token, "reference may outlive local value '" +
                                 statement.expression->children[0]->value + "'",
                                 "return the value itself or move ownership to the caller", "E0702");
                    }
                }
                break;
            case Stmt::Kind::If:
                require(expression_type(*statement.expression), Type::Bool, statement.expression->token, "if condition");
                analyze_block(statement.body);
                if (!statement.alternative.empty()) analyze_block(statement.alternative);
                break;
            case Stmt::Kind::While:
                require(expression_type(*statement.expression), Type::Bool, statement.expression->token, "while condition");
                ++loop_depth_; analyze_block(statement.body); --loop_depth_;
                break;
            case Stmt::Kind::Each: {
                require(expression_type(*statement.expression), Type::Int, statement.expression->token, "range start");
                require(expression_type(*statement.upper), Type::Int, statement.upper->token, "range end");
                scopes_.push_back({{statement.name, {Type::Int, false, statement.token, false, scopes_.size()}}});
                ++loop_depth_;
                for (Stmt &child : statement.body) analyze_statement(child);
                --loop_depth_;
                scopes_.pop_back();
                break;
            }
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue:
                if (loop_depth_ == 0) fail(statement.token, "'" + statement.token.text + "' requires an enclosing loop");
                break;
            case Stmt::Kind::Unsafe:
                ++unsafe_depth_; analyze_block(statement.body); --unsafe_depth_;
                break;
        }
    }

    Type expression_type(Expr &expression) {
        if (expression.inferred_type != Type::Infer) return expression.inferred_type;
        const Type inferred = infer_expression_type(expression);
        expression.inferred_type = inferred;
        return inferred;
    }

    Type infer_expression_type(Expr &expression) {
        switch (expression.kind) {
            case Expr::Kind::Integer:
                try { (void)std::stoll(expression.value); return Type::Int; }
                catch (const std::exception &) { fail(expression.token, "integer literal is outside signed 64-bit range"); }
            case Expr::Kind::Float:
                try { if (!std::isfinite(std::stod(expression.value))) fail(expression.token, "non-finite float literal"); }
                catch (const std::exception &) { fail(expression.token, "float literal is outside representable range"); }
                return Type::Float;
            case Expr::Kind::String: return Type::Str;
            case Expr::Kind::Boolean: return Type::Bool;
            case Expr::Kind::Variable: {
                const auto variable = lookup(expression.value);
                if (!variable) fail(expression.token, "unknown name '" + expression.value + "'", name_help(expression.value), "E0201");
                if (variable->moved)
                    fail(expression.token, "use of moved value '" + expression.value + "'",
                         "assign a new owning value before using this binding again", "E0703");
                return variable->type;
            }
            case Expr::Kind::Member:
                return member_type(expression_type(*expression.children[0]), expression);
            case Expr::Kind::Index:
                require(expression_type(*expression.children[0]), Type::Nums, expression.children[0]->token, "indexed value");
                require(expression_type(*expression.children[1]), Type::Int, expression.children[1]->token, "list index");
                return Type::Int;
            case Expr::Kind::List:
                for (const auto &child : expression.children)
                    require(expression_type(*child), Type::Int, child->token, "list element");
                return Type::Nums;
            case Expr::Kind::Call:
                return call_type(expression);
            case Expr::Kind::MethodCall:
                return method_call_type(expression);
            case Expr::Kind::SizeOf:
            case Expr::Kind::AlignOf:
                validate_type(Type{expression.value}, expression.token);
                return Type::Int;
            case Expr::Kind::Unary: {
                if (expression.value == "await") {
                    if (current_function_ == nullptr || (!current_function_->is_async && current_function_->name != "main"))
                        fail(expression.token, "await is only valid inside async functions or launch",
                             "mark the function `async fn` or await the task from launch", "E1503");
                    const Type task = expression_type(*expression.children[0]);
                    if (!is_task_type(task))
                        fail(expression.token, "await requires a Task value, got " + type_name(task), {}, "E1504");
                    return task_result_type(task);
                }
                if (expression.value == "-" && expression.children[0]->kind == Expr::Kind::Integer &&
                    expression.children[0]->value == "9223372036854775808") return Type::Int;
                if (expression.value == "&" || expression.value == "&mut" || expression.value == "&raw") {
                    Expr &target = *expression.children[0];
                    const Type inner = lvalue_type(target);
                    if (expression.value == "&mut" && !expression_is_mutable_lvalue(target))
                        fail(expression.token, "cannot create mutable reference to immutable value", {}, "E0701");
                    if (expression.value == "&raw") {
                        if (unsafe_depth_ == 0)
                            fail(expression.token, "creating a raw pointer requires unsafe context",
                                 "wrap `&raw` in unsafe { ... }", "E0800");
                        return Type{"*" + type_name(inner)};
                    }
                    return Type{std::string(expression.value == "&mut" ? "&mut " : "&") + type_name(inner)};
                }
                const Type operand = expression_type(*expression.children[0]);
                if (expression.value == "*") {
                    if (!is_pointer_like_type(operand)) fail(expression.token, "dereference requires pointer/reference");
                    if (is_raw_pointer_type(operand) && unsafe_depth_ == 0)
                        fail(expression.token, "raw pointer dereference requires unsafe context",
                             "wrap the operation in unsafe { ... }", "E0801");
                    return pointee_type(operand);
                }
                if (expression.value == "not") {
                    require(operand, Type::Bool, expression.children[0]->token, "logical negation");
                    return Type::Bool;
                }
                if (expression.value == "~") {
                    require(operand, Type::Int, expression.children[0]->token, "bitwise complement");
                    return Type::Int;
                }
                if (operand == Type::Int || operand == Type::Float) return operand;
                fail(expression.token, "operator '-' requires int or float");
            }
            case Expr::Kind::Binary: {
                const Type left = expression_type(*expression.children[0]);
                const std::string &op = expression.value;
                if (op == "and" || op == "or") {
                    require(left, Type::Bool, expression.children[0]->token, "logical operator");
                    require(expression_type(*expression.children[1]), Type::Bool, expression.children[1]->token, "logical operator");
                    return Type::Bool;
                }
                const Type right = expression_type(*expression.children[1]);
                if (is_raw_pointer_type(left) && right == Type::Int && (op == "+" || op == "-")) {
                    if (unsafe_depth_ == 0)
                        fail(expression.token, "raw pointer arithmetic requires unsafe context",
                             "wrap pointer arithmetic in unsafe { ... }", "E0803");
                    return left;
                }
                if (left != right) fail(expression.token, "operator '" + op + "' received " + type_name(left) + " and " + type_name(right));
                if (op == "==" || op == "!=") {
                    if (left != Type::Str && left != Type::Int && left != Type::Float && left != Type::Bool && !is_pointer_like_type(left) && !is_object(left))
                        fail(expression.token, "equality is defined only for scalar/pointer/object identity values");
                    return Type::Bool;
                }
                if (op == "<" || op == "<=" || op == ">" || op == ">=") {
                    if (left != Type::Int && left != Type::Float) fail(expression.token, "ordering requires int or float operands");
                    return Type::Bool;
                }
                if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
                    require(left, Type::Int, expression.token, "bitwise operator");
                    return Type::Int;
                }
                if (left == Type::Int) {
                    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") return Type::Int;
                    fail(expression.token, "invalid integer operator '" + op + "'");
                }
                if (left == Type::Str && op == "+") return Type::Str;
                if (left == Type::Float && (op == "+" || op == "-" || op == "*" || op == "/")) return Type::Float;
                fail(expression.token, "operator '" + op + "' requires numeric operands");
            }
        }
        throw Error("internal error: unknown expression kind");
    }

    Type lvalue_type(Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            auto variable = lookup(expression.value);
            if (!variable) fail(expression.token, "unknown name '" + expression.value + "'", name_help(expression.value), "E0201");
            expression.inferred_type = variable->type;
            return variable->type;
        }
        if (expression.kind == Expr::Kind::Member) {
            const Type type = member_type(expression_type(*expression.children[0]), expression);
            expression.inferred_type = type;
            return type;
        }
        if (expression.kind == Expr::Kind::Unary && expression.value == "*") return expression_type(expression);
        fail(expression.token, "address-of requires an addressable variable, field, or dereference", {}, "E0700");
    }

    Type call_type(Expr &expression) {
        if (expression.value == "cancel" || expression.value == "task_done") {
            if (expression.children.size() != 1)
                fail(expression.token, expression.value + " expects exactly one Task value");
            const Type task = expression_type(*expression.children[0]);
            if (!is_task_type(task))
                fail(expression.children[0]->token, expression.value + " requires a Task value, got " +
                     type_name(task), {}, "E1505");
            return expression.value == "task_done" ? Type::Bool : Type::Void;
        }
        if (expression.value == "move" || expression.value == "drop") {
            if (expression.children.size() != 1)
                fail(expression.token, expression.value + " expects exactly one owning binding");
            Expr &argument = *expression.children[0];
            if (argument.kind != Expr::Kind::Variable)
                fail(argument.token, expression.value + " requires a named owning binding",
                     "bind the value to a local variable first", "E0704");
            SemanticVariable *variable = lookup_mut(argument.value);
            if (!variable) fail(argument.token, "unknown name '" + argument.value + "'", name_help(argument.value), "E0201");
            if (variable->moved)
                fail(argument.token, "use of moved value '" + argument.value + "'",
                     "an owning binding can only be moved or dropped once", "E0703");
            const bool owning = variable->type == Type::Nums || is_object(variable->type);
            if (!owning)
                fail(argument.token, expression.value + " requires an owning object or nums value, got " +
                     type_name(variable->type), "scalar and value-struct values are copied", "E0704");
            argument.inferred_type = variable->type;
            variable->moved = true;
            return expression.value == "move" ? argument.inferred_type : Type::Void;
        }
        if (auto shape = shapes_.find(expression.value); shape != shapes_.end()) {
            const Shape &definition = *shape->second;
            if (definition.reference_type) {
                if (definition.initializer_name.empty()) {
                    if (!expression.children.empty()) fail(expression.token, "object '" + definition.name + "' has no init parameters");
                    return Type{definition.name};
                }
                const SemanticSignature &init = signatures_.at(definition.initializer_name);
                normalize_call_arguments(expression, init, 1, 0);
                for (std::size_t i = 0; i < expression.children.size(); ++i)
                    require(expression_type(*expression.children[i]), init.parameters[i + 1],
                            expression.children[i]->token, "constructor argument");
                return Type{definition.name};
            }
            normalize_struct_arguments(expression, definition);
            for (size_t i = 0; i < definition.fields.size(); ++i)
                require(expression_type(*expression.children[i]), definition.fields[i].type,
                        expression.children[i]->token, "field '" + definition.fields[i].name + "'");
            return Type{definition.name};
        }

        auto signature = signatures_.find(expression.value);
        if (signature == signatures_.end()) fail(expression.token, "unknown function '" + expression.value + "'", name_help(expression.value), "E0201");
        if (!signature->second.owner_type.empty()) fail(expression.token, "method must be called through an instance");
        check_call_arguments(expression, signature->second, 0);
        return signature->second.is_async ? task_type(signature->second.result) : signature->second.result;
    }

    Type method_call_type(Expr &expression) {
        if (expression.children.empty()) throw Error("internal error: method call without receiver");
        Type base = expression_type(*expression.children[0]);
        if (is_reference_type(base)) base = pointee_type(base);
        if (!shapes_.count(base.name)) fail(expression.token, "method call requires struct/object receiver, got " + base.name);
        const std::string lowered = base.name + "::" + expression.value;
        auto found = signatures_.find(lowered);
        if (found == signatures_.end()) {
            std::vector<std::string> candidates;
            const std::string prefix = base.name + "::";
            for (const auto &[name, signature] : signatures_) {
                (void)signature;
                if (name.rfind(prefix, 0) == 0) candidates.push_back(name.substr(prefix.size()));
            }
            const std::string closest = closest_name(expression.value, candidates);
            fail(expression.token, "type '" + base.name + "' has no method '" + expression.value + "'",
                 closest.empty() ? std::string{} : "did you mean `" + closest + "`?", "E0401");
        }
        const SemanticSignature &signature = found->second;
        if (signature.initializer) fail(expression.token, "init is a constructor and cannot be called as a normal method");
        if (signature.visibility == Visibility::Private && current_owner_ != base.name)
            fail(expression.token, "method '" + base.name + "." + expression.value + "' is private", {}, "E1303");
        if (signature.self_mutable && !expression_is_mutable_lvalue(*expression.children[0]))
            fail(expression.token, "method '" + expression.value + "' requires mutable self",
                 "declare the receiver with `let mut`", "E1402");
        check_call_arguments(expression, signature, 1);
        expression.value = lowered; // backend receives canonical method symbol
        return signature.is_async ? task_type(signature.result) : signature.result;
    }

    void check_call_arguments(Expr &expression, const SemanticSignature &signature, std::size_t self_count) {
        normalize_call_arguments(expression, signature, self_count, self_count);
        const std::size_t actual_count = expression.children.size() - self_count;
        const std::size_t expected_count = signature.parameters.size() - self_count;
        if (actual_count != expected_count)
            fail(expression.token, "call expects " + std::to_string(expected_count) + " argument(s), got " + std::to_string(actual_count));
        for (std::size_t i = 0; i < actual_count; ++i) {
            Expr &argument = *expression.children[i + self_count];
            const Type actual = expression_type(argument);
            if (actual == Type::Void) fail(argument.token, "void cannot be passed as an argument");
            const Type expected = signature.parameters[i + self_count];
            if (expected != Type::Infer) require(actual, expected, argument.token, "argument");
            else if (expression.value == "print" || expression.value == "println") {
                if (actual != Type::Int && actual != Type::Float && actual != Type::Bool && actual != Type::Str)
                    fail(argument.token, "print requires a scalar value");
            }
        }
    }

    static std::unique_ptr<Expr> clone_expression(const Expr &source) {
        auto result = std::make_unique<Expr>();
        result->kind = source.kind;
        result->token = source.token;
        result->value = source.value;
        result->argument_names = source.argument_names;
        result->inferred_type = Type::Infer;
        for (const auto &child : source.children) result->children.push_back(clone_expression(*child));
        return result;
    }

    void normalize_call_arguments(Expr &expression, const SemanticSignature &signature,
                                  std::size_t signature_offset, std::size_t expression_prefix) {
        if (expression.argument_names.size() < expression.children.size())
            expression.argument_names.resize(expression.children.size());
        const std::size_t expected = signature.parameters.size() - signature_offset;
        const std::size_t actual = expression.children.size() - expression_prefix;
        std::size_t required = expected;
        while (required > 0 && signature.defaults.size() > signature_offset + required - 1 &&
               signature.defaults[signature_offset + required - 1]) --required;
        if (actual > expected)
            fail(expression.token, "call expects " + std::to_string(expected) + " argument(s), got " + std::to_string(actual));

        std::vector<std::unique_ptr<Expr>> ordered(expected);
        std::size_t next = 0;
        for (std::size_t i = expression_prefix; i < expression.children.size(); ++i) {
            const std::string &name = expression.argument_names[i];
            std::size_t destination = next;
            if (!name.empty()) {
                destination = expected;
                for (std::size_t p = signature_offset; p < signature.parameter_names.size(); ++p)
                    if (signature.parameter_names[p] == name) { destination = p - signature_offset; break; }
                if (destination >= expected)
                    fail(expression.children[i]->token, "unknown named argument '" + name + "'", {}, "E1201");
            } else {
                while (destination < ordered.size() && ordered[destination]) ++destination;
                next = destination + 1;
            }
            if (destination >= ordered.size())
                fail(expression.token, "call expects " + std::to_string(expected) + " argument(s), got " + std::to_string(actual));
            if (ordered[destination])
                fail(expression.children[i]->token, "argument '" + signature.parameter_names[signature_offset + destination] + "' was supplied more than once", {}, "E1202");
            ordered[destination] = std::move(expression.children[i]);
        }
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (ordered[i]) continue;
            const std::size_t signature_index = signature_offset + i;
            if (signature.defaults.size() > signature_index && signature.defaults[signature_index])
                ordered[i] = clone_expression(*signature.defaults[signature_index]);
            else {
                const std::string label = signature.parameter_names.size() > signature_index ? signature.parameter_names[signature_index] : std::to_string(i + 1);
                if (required == expected)
                    fail(expression.token, "call expects " + std::to_string(expected) + " argument(s), got " + std::to_string(actual));
                fail(expression.token, "missing required argument '" + label + "'", {}, "E1203");
            }
        }
        std::vector<std::unique_ptr<Expr>> normalized;
        normalized.reserve(expression_prefix + ordered.size());
        for (std::size_t i = 0; i < expression_prefix; ++i) normalized.push_back(std::move(expression.children[i]));
        for (auto &argument : ordered) normalized.push_back(std::move(argument));
        expression.children = std::move(normalized);
        expression.argument_names.assign(expression.children.size(), "");
    }

    void normalize_struct_arguments(Expr &expression, const Shape &shape) {
        SemanticSignature signature;
        signature.parameters.reserve(shape.fields.size());
        signature.parameter_names.reserve(shape.fields.size());
        signature.defaults.resize(shape.fields.size());
        for (const Parameter &field : shape.fields) {
            signature.parameters.push_back(field.type);
            signature.parameter_names.push_back(field.name);
        }
        normalize_call_arguments(expression, signature, 0, 0);
    }
};

#endif  // PUNPUN_SEMANTIC_HPP
