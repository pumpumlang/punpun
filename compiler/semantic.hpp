#ifndef PUNPUN_SEMANTIC_HPP
#define PUNPUN_SEMANTIC_HPP

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <deque>
#include <optional>
#include <sstream>
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
        collect_generic_declarations();
        materialize_referenced_types();
        collect_shapes();
        validate_materialized_constraints();
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
            for (Function &function : module.functions)
                if (function.generic_parameters.empty() && !generic_owner(function.owner_type)) analyze_function(function);
        for (std::size_t i = 0; i < specializations_.size(); ++i) analyze_function(specializations_[i]);
        for (Module &module : modules_) {
            module.functions.erase(std::remove_if(module.functions.begin(), module.functions.end(), [&](const Function &function) {
                return !function.generic_parameters.empty() || generic_owner(function.owner_type);
            }), module.functions.end());
            module.shapes.erase(std::remove_if(module.shapes.begin(), module.shapes.end(), [](const Shape &shape) {
                return !shape.generic_parameters.empty();
            }), module.shapes.end());
            module.enums.clear();
        }
        if (!materialized_shapes_.empty() || !specializations_.empty()) {
            Module generated;
            generated.file = modules_.empty() ? fs::path{} : modules_.front().file;
            for (Shape &shape : materialized_shapes_) generated.shapes.push_back(std::move(shape));
            for (Function &function : specializations_) generated.functions.push_back(std::move(function));
            modules_.push_back(std::move(generated));
        }
    }

    const std::unordered_map<std::string, SemanticSignature> &signatures() const { return signatures_; }
    const std::unordered_map<std::string, const Shape *> &shapes() const { return shapes_; }

  private:
    std::vector<Module> &modules_;
    std::unordered_map<std::string, SemanticSignature> signatures_;
    std::unordered_map<std::string, const Shape *> shapes_;
    std::unordered_map<std::string, const Contract *> contracts_;
    std::unordered_map<std::string, const Function *> generic_functions_;
    std::unordered_map<std::string, const Shape *> generic_shapes_;
    std::unordered_map<std::string, const EnumDecl *> enum_templates_;
    std::deque<EnumDecl> builtin_enums_;
    std::deque<Shape> materialized_shapes_;
    std::deque<Function> specializations_;
    std::deque<Function> owned_generic_functions_;
    std::unordered_set<std::string> specialization_keys_;
    std::unordered_set<std::string> materialized_names_;
    bool shape_map_ready_ = false;
    bool function_map_ready_ = false;
    std::unordered_map<std::string, int> shape_state_;
    std::vector<std::unordered_map<std::string, SemanticVariable>> scopes_;
    Type current_result_ = Type::Void;
    std::string current_owner_;
    const Function *current_function_ = nullptr;
    int loop_depth_ = 0;
    int unsafe_depth_ = 0;
    Type expected_type_ = Type::Infer;
    std::size_t specialization_work_ = 0;

    struct TypeParts { std::string base; std::vector<Type> arguments; };

    static TypeParts split_type(const Type &type) {
        TypeParts result{type.name, {}};
        const std::size_t open = type.name.find('<');
        if (open == std::string::npos || type.name.back() != '>') return result;
        result.base = type.name.substr(0, open);
        std::size_t start = open + 1;
        int depth = 0;
        for (std::size_t i = start; i + 1 < type.name.size(); ++i) {
            if (type.name[i] == '<') ++depth;
            else if (type.name[i] == '>') --depth;
            else if (type.name[i] == ',' && depth == 0) {
                result.arguments.push_back(Type{type.name.substr(start, i - start)});
                start = i + 1;
            }
        }
        result.arguments.push_back(Type{type.name.substr(start, type.name.size() - start - 1)});
        return result;
    }

    static std::string constructed_name(const std::string &base, const std::vector<Type> &arguments) {
        if (arguments.empty()) return base;
        std::string result = base + "<";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i) result += ",";
            result += arguments[i].name;
        }
        return result + ">";
    }

    static Type substitute_type(Type type, const std::unordered_map<std::string, Type> &bindings) {
        if (auto found = bindings.find(type.name); found != bindings.end()) return found->second;
        if (is_pointer_like_type(type)) {
            const Type inner = substitute_type(pointee_type(type), bindings);
            if (is_mut_reference_type(type)) return Type{"&mut " + inner.name};
            return Type{type.name.substr(0, 1) + inner.name};
        }
        const TypeParts parts = split_type(type);
        if (parts.arguments.empty()) return type;
        std::vector<Type> arguments;
        for (Type argument : parts.arguments) arguments.push_back(substitute_type(argument, bindings));
        return Type{constructed_name(parts.base, arguments)};
    }

    bool generic_owner(const std::string &owner) const { return !owner.empty() && generic_shapes_.count(owner); }

    [[noreturn]] static void fail(const Token &token, const std::string &message,
                                  const std::string &help = {}, const std::string &code = "E1000",
                                  const std::string &fix_replacement = {}) {
        throw Error(ppdiag::format(token.file, token.line, token.column, token.length,
                                   code, message, message, help, ppdiag::Severity::Error, fix_replacement));
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

    std::string name_suggestion(const std::string &name) const {
        std::vector<std::string> candidates;
        for (const auto &scope : scopes_) for (const auto &[candidate, variable] : scope) { (void)variable; candidates.push_back(candidate); }
        for (const auto &[candidate, signature] : signatures_) { (void)signature; candidates.push_back(candidate); }
        for (const auto &[candidate, shape] : shapes_) { (void)shape; candidates.push_back(candidate); }
        return closest_name(name, candidates);
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

    void add_builtin_enum_templates() {
        const Token token = builtin_token();
        EnumDecl option;
        option.token = token; option.name = "Option";
        option.generic_parameters.push_back(GenericParameter{token, "T", {}});
        option.variants.push_back(EnumVariant{token, "None", {}});
        option.variants.push_back(EnumVariant{token, "Some", {Type{"T"}}});
        builtin_enums_.push_back(std::move(option));

        EnumDecl result;
        result.token = token; result.name = "Result";
        result.generic_parameters.push_back(GenericParameter{token, "T", {}});
        result.generic_parameters.push_back(GenericParameter{token, "E", {}});
        result.variants.push_back(EnumVariant{token, "Ok", {Type{"T"}}});
        result.variants.push_back(EnumVariant{token, "Error", {Type{"E"}}});
        builtin_enums_.push_back(std::move(result));
        for (const EnumDecl &declaration : builtin_enums_) enum_templates_[declaration.name] = &declaration;
    }

    void collect_generic_declarations() {
        add_builtin_enum_templates();
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) {
                if (!shape.generic_parameters.empty()) generic_shapes_[shape.name] = &shape;
            }
            for (const EnumDecl &declaration : module.enums) {
                if (enum_templates_.count(declaration.name) || generic_shapes_.count(declaration.name) || contracts_.count(declaration.name))
                    fail(declaration.token, "duplicate enum/type/contract name '" + declaration.name + "'", {}, "E0202");
                enum_templates_[declaration.name] = &declaration;
            }
            for (const Function &function : module.functions) {
                if (!function.generic_parameters.empty() || generic_owner(function.owner_type)) {
                    if (generic_functions_.count(function.name))
                        fail(function.token, "duplicate generic function '" + function.name + "'", {}, "E0202");
                    generic_functions_[function.name] = &function;
                }
            }
        }
        for (const auto &[name, shape] : generic_shapes_) {
            if (enum_templates_.count(name) || contracts_.count(name))
                fail(shape->token, "duplicate generic type/enum/contract name '" + name + "'", {}, "E0202");
        }
    }

    void ensure_concrete_type(Type type) {
        if (is_task_type(type)) { ensure_concrete_type(task_result_type(type)); return; }
        if (is_pointer_like_type(type)) { ensure_concrete_type(pointee_type(type)); return; }
        const TypeParts parts = split_type(type);
        for (Type argument : parts.arguments) ensure_concrete_type(argument);
        if (enum_templates_.count(parts.base)) materialize_enum(type, *enum_templates_.at(parts.base), parts.arguments);
        else if (generic_shapes_.count(parts.base)) materialize_shape(type, *generic_shapes_.at(parts.base), parts.arguments);
    }

    std::unordered_map<std::string, Type> type_bindings(const std::vector<GenericParameter> &parameters,
                                                        const std::vector<Type> &arguments,
                                                        const Token &token) const {
        if (parameters.size() != arguments.size())
            fail(token, "generic declaration expects " + std::to_string(parameters.size()) +
                 " type argument(s), got " + std::to_string(arguments.size()), {}, "E1600");
        std::unordered_map<std::string, Type> result;
        for (std::size_t i = 0; i < parameters.size(); ++i) result[parameters[i].name] = arguments[i];
        return result;
    }

    void materialize_enum(Type concrete, const EnumDecl &declaration, const std::vector<Type> &arguments) {
        if (materialized_names_.count(concrete.name)) return;
        materialized_names_.insert(concrete.name);
        const auto bindings = type_bindings(declaration.generic_parameters, arguments, declaration.token);
        Shape shape;
        shape.token = declaration.token;
        shape.name = concrete.name;
        shape.enum_type = true;
        Parameter tag{declaration.token, "__tag", Type::Int, true, Visibility::Private, {}};
        shape.fields.push_back(std::move(tag));
        shape.enum_variants = declaration.variants;
        for (std::size_t i = 0; i < shape.enum_variants.size(); ++i) {
            EnumVariant &variant = shape.enum_variants[i];
            for (std::size_t j = 0; j < variant.payload.size(); ++j) {
                variant.payload[j] = substitute_type(variant.payload[j], bindings);
                ensure_concrete_type(variant.payload[j]);
                shape.fields.push_back(Parameter{variant.token, "__v" + std::to_string(i) + "_" + std::to_string(j),
                                                 variant.payload[j], true, Visibility::Private, {}});
            }
        }
        materialized_shapes_.push_back(std::move(shape));
        if (shape_map_ready_) shapes_[materialized_shapes_.back().name] = &materialized_shapes_.back();
    }

    void materialize_shape(Type concrete, const Shape &declaration, const std::vector<Type> &arguments) {
        if (materialized_names_.count(concrete.name)) return;
        const auto bindings = type_bindings(declaration.generic_parameters, arguments, declaration.token);
        Shape shape = declaration;
        shape.name = concrete.name;
        shape.generic_parameters.clear();
        shape.method_names.clear();
        shape.initializer_name.clear();
        materialized_names_.insert(shape.name);
        for (Parameter &field : shape.fields) {
            field.type = substitute_type(field.type, bindings);
            ensure_concrete_type(field.type);
        }
        materialized_shapes_.push_back(std::move(shape));
        Shape &materialized = materialized_shapes_.back();
        if (shape_map_ready_) shapes_[materialized.name] = &materialized;
        for (const Module &module : modules_) for (const Function &source : module.functions) {
            if (source.owner_type != declaration.name) continue;
            Function method = clone_function(source);
            method.owner_type = concrete.name;
            method.name = concrete.name + "::" + method.source_name;
            method.result = substitute_type(method.result, bindings);
            for (Parameter &parameter : method.parameters) {
                parameter.type = substitute_type(parameter.type, bindings);
                if (parameter.name == "self") {
                    if (is_mut_reference_type(parameter.type)) parameter.type = Type{"&mut " + concrete.name};
                    else if (is_reference_type(parameter.type)) parameter.type = Type{"&" + concrete.name};
                    else parameter.type = concrete;
                }
                if (parameter.default_value) substitute_expression(*parameter.default_value, bindings);
            }
            for (Stmt &statement : method.body) substitute_statement(statement, bindings);
            materialized.method_names.push_back(method.name);
            if (method.is_initializer) materialized.initializer_name = method.name;
            if (method.generic_parameters.empty()) {
                specializations_.push_back(std::move(method));
                if (function_map_ready_) register_concrete_function(specializations_.back());
            }
            else {
                owned_generic_functions_.push_back(std::move(method));
                generic_functions_[owned_generic_functions_.back().name] = &owned_generic_functions_.back();
            }
        }
    }

    void scan_expression_types(const Expr &expression) {
        for (Type type : expression.type_arguments) ensure_concrete_type(type);
        if (expression.kind == Expr::Kind::SizeOf || expression.kind == Expr::Kind::AlignOf)
            ensure_concrete_type(Type{expression.value});
        for (const auto &child : expression.children) scan_expression_types(*child);
    }

    void scan_statement_types(const Stmt &statement) {
        if (statement.declared_type != Type::Infer) ensure_concrete_type(statement.declared_type);
        if (statement.expression) scan_expression_types(*statement.expression);
        if (statement.target) scan_expression_types(*statement.target);
        if (statement.upper) scan_expression_types(*statement.upper);
        for (const Stmt &child : statement.body) scan_statement_types(child);
        for (const Stmt &child : statement.alternative) scan_statement_types(child);
    }

    void materialize_referenced_types() {
        for (const auto &[name, declaration] : enum_templates_)
            if (declaration->generic_parameters.empty()) materialize_enum(Type{name}, *declaration, {});
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) if (shape.generic_parameters.empty())
                for (const Parameter &field : shape.fields) ensure_concrete_type(field.type);
            for (const Function &function : module.functions) if (function.generic_parameters.empty() && !generic_owner(function.owner_type)) {
                ensure_concrete_type(function.result);
                for (const Parameter &parameter : function.parameters) ensure_concrete_type(parameter.type);
                for (const Stmt &statement : function.body) scan_statement_types(statement);
            }
        }
    }

    void collect_shapes() {
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) {
                if (!shape.generic_parameters.empty()) continue;
                if (shapes_.count(shape.name) || contracts_.count(shape.name) || signatures_.count(shape.name))
                    fail(shape.token, "duplicate type/contract or built-in name '" + shape.name + "'");
                shapes_[shape.name] = &shape;
            }
        }
        for (const Shape &shape : materialized_shapes_) {
            if (shapes_.count(shape.name)) fail(shape.token, "duplicate materialized type '" + shape.name + "'", {}, "E0202");
            shapes_[shape.name] = &shape;
        }
        shape_map_ready_ = true;
    }

    void validate_materialized_constraints() {
        for (const Shape &shape : materialized_shapes_) {
            const TypeParts parts = split_type(Type{shape.name});
            auto source = generic_shapes_.find(parts.base);
            if (source == generic_shapes_.end()) continue;
            const auto bindings = type_bindings(source->second->generic_parameters, parts.arguments, shape.token);
            for (std::size_t i = 0; i < source->second->generic_parameters.size(); ++i) {
                const GenericParameter &parameter = source->second->generic_parameters[i];
                for (Type constraint : parameter.constraints) {
                    constraint = substitute_type(constraint, bindings);
                    if (!satisfies_constraint(parts.arguments[i], constraint))
                        fail(shape.token, "type '" + parts.arguments[i].name + "' does not satisfy constraint '" + constraint.name + "'", {}, "E1604");
                }
            }
        }
    }

    void collect_functions() {
        for (const Module &module : modules_) {
            for (const Function &function : module.functions) {
                if (!function.generic_parameters.empty() || generic_owner(function.owner_type)) continue;
                register_concrete_function(function);
            }
        }
        for (const Function &function : specializations_)
            if (!signatures_.count(function.name)) register_concrete_function(function);
        function_map_ready_ = true;
    }

    void register_concrete_function(const Function &function) {
        if (signatures_.count(function.name) || (!function.is_method && shapes_.count(function.name)))
            fail(function.token, "duplicate function '" + function.name + "'");
        validate_type(function.result, function.token, true);
        SemanticSignature signature{{}, {}, {}, function.result, function.token, false,
                                    function.owner_type, function.self_mutable,
                                    function.is_initializer, function.visibility};
        signature.is_async = function.is_async;
        if (function.is_async) {
            if (function.is_initializer) fail(function.token, "constructors cannot be async", {}, "E1500");
            if (function.name == "main") fail(function.token, "the program entry function cannot be async",
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

    void validate_contracts() {
        for (const auto &[name, contract] : contracts_) {
            (void)name;
            if (!contract->generic_parameters.empty()) continue;
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

    void validate_type(Type type, const Token &token, bool allow_void = false) {
        ensure_concrete_type(type);
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
        if (is_slice_type(type)) {
            const Type inner = slice_element_type(type);
            if (inner != Type::Int)
                fail(token, "PunPun currently supports Slice<int> only",
                     "use nums + view(...) for the 0.6 slice implementation", "E0710");
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
                if (statement.declared_type != Type::Infer) ensure_concrete_type(statement.declared_type);
                const Type value = expression_type_as(*statement.expression, statement.declared_type);
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
                    const Type target = assignment_target(*statement.target);
                    const Type value = expression_type(*statement.expression);
                    require(value, target, statement.expression->token, "assignment");
                    if (statement.assignment_op != "=" && statement.assignment_op != "<-") {
                        if (target != Type::Int && target != Type::Float)
                            fail(statement.token, "compound assignment requires numeric target");
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
                    const Type value = expression_type_as(*statement.expression, current_result_);
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

    Type expression_type_as(Expr &expression, Type expected) {
        if (expected == Type::Infer) return expression_type(expression);
        const Type previous = expected_type_;
        expected_type_ = expected;
        const Type result = expression_type(expression);
        expected_type_ = previous;
        return result;
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
                if (const std::size_t separator = expression.value.rfind("::"); separator != std::string::npos) {
                    const std::string owner = expression.value.substr(0, separator);
                    if (enum_templates_.count(split_type(Type{owner}).base))
                        return enum_constructor_type(expression, owner, expression.value.substr(separator + 2));
                }
                const auto variable = lookup(expression.value);
                if (!variable) {
                    const std::string suggestion = name_suggestion(expression.value);
                    fail(expression.token, "unknown name '" + expression.value + "'",
                         suggestion.empty() ? std::string{} : "did you mean `" + suggestion + "`?",
                         "E0201", suggestion);
                }
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
            case Expr::Kind::EnumConstruct:
                throw Error("internal error: enum constructor was analyzed twice");
            case Expr::Kind::Match:
                return match_type(expression);
            case Expr::Kind::Propagate:
                return propagate_type(expression);
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
                    expression.children[0]->value == "9223372036854775808") {
                    // The magnitude is one past INT64_MAX, but the full unary
                    // expression is exactly INT64_MIN. Mark the child typed so
                    // mandatory HIR/MIR verification can represent the syntax;
                    // native backends already lower this pair as INT64_MIN.
                    expression.children[0]->inferred_type = Type::Int;
                    return Type::Int;
                }
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
            if (!variable) {
                    const std::string suggestion = name_suggestion(expression.value);
                    fail(expression.token, "unknown name '" + expression.value + "'",
                         suggestion.empty() ? std::string{} : "did you mean `" + suggestion + "`?",
                         "E0201", suggestion);
                }
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

    static bool generic_parameter_named(const Function &function, const std::string &name) {
        return std::any_of(function.generic_parameters.begin(), function.generic_parameters.end(),
                           [&](const GenericParameter &parameter) { return parameter.name == name; });
    }

    static bool infer_binding(Type pattern, Type actual, const Function &function,
                              std::unordered_map<std::string, Type> &bindings) {
        if (generic_parameter_named(function, pattern.name)) {
            auto found = bindings.find(pattern.name);
            if (found == bindings.end()) { bindings[pattern.name] = actual; return true; }
            return found->second == actual;
        }
        if (is_pointer_like_type(pattern) || is_pointer_like_type(actual)) {
            if (is_mut_reference_type(pattern) != is_mut_reference_type(actual) ||
                is_reference_type(pattern) != is_reference_type(actual) ||
                is_raw_pointer_type(pattern) != is_raw_pointer_type(actual)) return false;
            return infer_binding(pointee_type(pattern), pointee_type(actual), function, bindings);
        }
        const TypeParts left = split_type(pattern), right = split_type(actual);
        if (left.base != right.base || left.arguments.size() != right.arguments.size()) return false;
        if (left.arguments.empty()) return pattern == actual;
        for (std::size_t i = 0; i < left.arguments.size(); ++i)
            if (!infer_binding(left.arguments[i], right.arguments[i], function, bindings)) return false;
        return true;
    }

    bool satisfies_constraint(Type actual, Type constraint) const {
        const TypeParts parts = split_type(constraint);
        if (parts.base == "Copy") {
            if (actual == Type::Int || actual == Type::Float || actual == Type::Bool || actual == Type::Str || is_pointer_like_type(actual)) return true;
            auto shape = shapes_.find(actual.name);
            if (shape == shapes_.end() || shape->second->reference_type) return false;
            for (const Parameter &field : shape->second->fields)
                if (!satisfies_constraint(field.type, Type{"Copy"})) return false;
            return true;
        }
        if (parts.base == "Comparable" || parts.base == "Equatable")
            return actual == Type::Int || actual == Type::Float || actual == Type::Bool || actual == Type::Str || is_pointer_like_type(actual) || is_object(actual);
        auto shape = shapes_.find(actual.name);
        if (shape == shapes_.end()) return false;
        return std::any_of(shape->second->contracts.begin(), shape->second->contracts.end(),
                           [&](const std::string &name) { return split_type(Type{name}).base == parts.base; });
    }

    static std::string specialization_name(const std::string &name, const std::vector<Type> &arguments) {
        std::string result = name + "$";
        for (const Type &argument : arguments)
            result += std::to_string(argument.name.size()) + "_" + argument.name + "$";
        return result;
    }

    static Stmt clone_statement(const Stmt &source) {
        Stmt result{source.kind, source.token};
        result.name = source.name;
        result.declared_type = source.declared_type;
        result.mutable_value = source.mutable_value;
        result.constant_value = source.constant_value;
        result.assignment_op = source.assignment_op;
        if (source.expression) result.expression = clone_expression(*source.expression);
        if (source.target) result.target = clone_expression(*source.target);
        if (source.upper) result.upper = clone_expression(*source.upper);
        for (const Stmt &child : source.body) result.body.push_back(clone_statement(child));
        for (const Stmt &child : source.alternative) result.alternative.push_back(clone_statement(child));
        return result;
    }

    static Function clone_function(const Function &source) {
        Function result;
        result.token = source.token; result.name = source.name; result.source_name = source.source_name;
        result.owner_type = source.owner_type; result.generic_parameters = source.generic_parameters;
        result.result = source.result; result.is_method = source.is_method; result.is_initializer = source.is_initializer;
        result.self_mutable = source.self_mutable; result.visibility = source.visibility;
        result.is_extern_native = source.is_extern_native; result.is_async = source.is_async; result.native_symbol = source.native_symbol;
        for (const Parameter &parameter : source.parameters) {
            Parameter copy = parameter;
            if (parameter.default_value) copy.default_value = std::shared_ptr<Expr>(clone_expression(*parameter.default_value).release());
            result.parameters.push_back(std::move(copy));
        }
        for (const Stmt &statement : source.body) result.body.push_back(clone_statement(statement));
        return result;
    }

    static void substitute_expression(Expr &expression, const std::unordered_map<std::string, Type> &bindings) {
        for (Type &type : expression.type_arguments) type = substitute_type(type, bindings);
        if (expression.kind == Expr::Kind::SizeOf || expression.kind == Expr::Kind::AlignOf)
            expression.value = substitute_type(Type{expression.value}, bindings).name;
        for (auto &child : expression.children) substitute_expression(*child, bindings);
    }

    static void substitute_statement(Stmt &statement, const std::unordered_map<std::string, Type> &bindings) {
        statement.declared_type = substitute_type(statement.declared_type, bindings);
        if (statement.expression) substitute_expression(*statement.expression, bindings);
        if (statement.target) substitute_expression(*statement.target, bindings);
        if (statement.upper) substitute_expression(*statement.upper, bindings);
        for (Stmt &child : statement.body) substitute_statement(child, bindings);
        for (Stmt &child : statement.alternative) substitute_statement(child, bindings);
    }

    Type specialize_generic_call(Expr &expression, const Function &function, std::size_t self_count) {
        SemanticSignature pattern{{}, {}, {}, function.result, function.token, false, function.owner_type,
                                  function.self_mutable, function.is_initializer, function.visibility, function.is_async};
        for (const Parameter &parameter : function.parameters) {
            pattern.parameters.push_back(parameter.type); pattern.parameter_names.push_back(parameter.name); pattern.defaults.push_back(parameter.default_value);
        }
        normalize_call_arguments(expression, pattern, self_count, self_count);
        if (expression.children.size() != pattern.parameters.size())
            fail(expression.token, "generic call expects " + std::to_string(pattern.parameters.size() - self_count) +
                 " argument(s), got " + std::to_string(expression.children.size() - self_count), {}, "E1601");

        std::unordered_map<std::string, Type> bindings;
        if (!expression.type_arguments.empty()) {
            if (expression.type_arguments.size() != function.generic_parameters.size())
                fail(expression.token, "explicit generic call supplies the wrong number of type arguments", {}, "E1600");
            for (std::size_t i = 0; i < expression.type_arguments.size(); ++i)
                bindings[function.generic_parameters[i].name] = expression.type_arguments[i];
        }
        for (std::size_t i = self_count; i < expression.children.size(); ++i) {
            const Type actual = expression_type(*expression.children[i]);
            if (!infer_binding(pattern.parameters[i], actual, function, bindings))
                fail(expression.children[i]->token, "generic argument inference conflicts for parameter '" +
                     pattern.parameter_names[i] + "'", {}, "E1602");
        }
        if (expected_type_ != Type::Infer) (void)infer_binding(function.result, expected_type_, function, bindings);

        std::vector<Type> arguments;
        for (const GenericParameter &parameter : function.generic_parameters) {
            auto found = bindings.find(parameter.name);
            if (found == bindings.end()) fail(expression.token, "cannot infer generic type parameter '" + parameter.name + "'",
                                              "supply it explicitly as <Type>", "E1603");
            ensure_concrete_type(found->second);
            for (Type constraint : parameter.constraints) {
                constraint = substitute_type(constraint, bindings);
                const std::string constraint_base = split_type(constraint).base;
                if (constraint_base != "Copy" && constraint_base != "Comparable" && constraint_base != "Equatable" &&
                    !contracts_.count(constraint_base))
                    fail(parameter.token, "unknown generic constraint '" + constraint_base + "'", {}, "E1604");
                if (!satisfies_constraint(found->second, constraint))
                    fail(expression.token, "type '" + found->second.name + "' does not satisfy constraint '" + constraint.name + "'", {}, "E1604");
            }
            arguments.push_back(found->second);
        }
        const std::string name = specialization_name(function.name, arguments);
        if (!signatures_.count(name)) {
            if (++specialization_work_ > 256)
                fail(expression.token, "generic specialization limit exceeded", "check for infinitely expanding generic recursion", "E1605");
            Function specialized = clone_function(function);
            specialized.name = name;
            specialized.owner_type = substitute_type(Type{function.owner_type}, bindings).name;
            specialized.generic_parameters.clear();
            specialized.result = substitute_type(specialized.result, bindings);
            ensure_concrete_type(specialized.result);
            for (Parameter &parameter : specialized.parameters) {
                parameter.type = substitute_type(parameter.type, bindings);
                ensure_concrete_type(parameter.type);
                if (parameter.default_value) substitute_expression(*parameter.default_value, bindings);
            }
            for (Stmt &statement : specialized.body) substitute_statement(statement, bindings);
            SemanticSignature signature{{}, {}, {}, specialized.result, specialized.token, false, specialized.owner_type,
                                        specialized.self_mutable, specialized.is_initializer, specialized.visibility, specialized.is_async};
            for (const Parameter &parameter : specialized.parameters) {
                signature.parameters.push_back(parameter.type); signature.parameter_names.push_back(parameter.name); signature.defaults.push_back(parameter.default_value);
            }
            signatures_[name] = std::move(signature);
            specializations_.push_back(std::move(specialized));
        }
        expression.value = name;
        expression.type_arguments.clear();
        check_call_arguments(expression, signatures_.at(name), self_count);
        const SemanticSignature &signature = signatures_.at(name);
        return signature.is_async ? task_type(signature.result) : signature.result;
    }

    Type enum_constructor_type(Expr &expression, const std::string &owner, const std::string &variant_name) {
        TypeParts owner_parts = split_type(Type{owner});
        auto declaration_it = enum_templates_.find(owner_parts.base);
        if (declaration_it == enum_templates_.end()) return Type::Infer;
        const EnumDecl &declaration = *declaration_it->second;
        const EnumVariant *variant = nullptr;
        for (const EnumVariant &candidate : declaration.variants) if (candidate.name == variant_name) { variant = &candidate; break; }
        if (!variant) fail(expression.token, "enum '" + owner_parts.base + "' has no variant '" + variant_name + "'", {}, "E1700");
        if (variant->payload.size() != expression.children.size())
            fail(expression.token, "variant '" + variant_name + "' expects " + std::to_string(variant->payload.size()) +
                 " value(s), got " + std::to_string(expression.children.size()), {}, "E1701");

        std::unordered_map<std::string, Type> bindings;
        const std::vector<Type> explicit_arguments = !expression.type_arguments.empty() ? expression.type_arguments : owner_parts.arguments;
        if (!explicit_arguments.empty()) bindings = type_bindings(declaration.generic_parameters, explicit_arguments, expression.token);
        if (expected_type_ != Type::Infer) {
            const TypeParts expected = split_type(expected_type_);
            if (expected.base == owner_parts.base && expected.arguments.size() == declaration.generic_parameters.size())
                for (std::size_t i = 0; i < expected.arguments.size(); ++i)
                    bindings[declaration.generic_parameters[i].name] = expected.arguments[i];
        }
        Function inference;
        inference.generic_parameters = declaration.generic_parameters;
        for (std::size_t i = 0; i < variant->payload.size(); ++i) {
            const Type actual = expression_type(*expression.children[i]);
            if (!infer_binding(variant->payload[i], actual, inference, bindings))
                fail(expression.children[i]->token, "enum payload conflicts with inferred generic arguments", {}, "E1702");
        }
        std::vector<Type> arguments;
        for (const GenericParameter &parameter : declaration.generic_parameters) {
            auto found = bindings.find(parameter.name);
            if (found == bindings.end()) fail(expression.token, "cannot infer enum type parameter '" + parameter.name + "'",
                                              "qualify the constructor, for example " + owner_parts.base + "<Type>::" + variant_name, "E1703");
            arguments.push_back(found->second);
        }
        const Type concrete{constructed_name(owner_parts.base, arguments)};
        ensure_concrete_type(concrete);
        const Shape &shape = *shapes_.at(concrete.name);
        std::size_t variant_index = 0;
        while (shape.enum_variants[variant_index].name != variant_name) ++variant_index;
        for (std::size_t i = 0; i < expression.children.size(); ++i)
            require(expression_type_as(*expression.children[i], shape.enum_variants[variant_index].payload[i]),
                    shape.enum_variants[variant_index].payload[i], expression.children[i]->token, "variant payload");
        expression.kind = Expr::Kind::EnumConstruct;
        expression.value = concrete.name;
        expression.enum_variant = variant_name;
        expression.type_arguments.clear();
        return concrete;
    }

    void bind_pattern(Pattern &pattern, Type subject, std::unordered_set<std::string> &bindings) {
        if (pattern.kind == Pattern::Kind::Wildcard) return;
        if (pattern.kind == Pattern::Kind::Binding) {
            if (!bindings.insert(pattern.value).second) fail(pattern.token, "duplicate binding '" + pattern.value + "' in pattern", {}, "E1710");
            scopes_.back()[pattern.value] = {subject, false, pattern.token, false, scopes_.size() - 1};
            return;
        }
        if (pattern.kind == Pattern::Kind::Integer) { require(subject, Type::Int, pattern.token, "integer pattern"); return; }
        if (pattern.kind == Pattern::Kind::String) { require(subject, Type::Str, pattern.token, "string pattern"); return; }
        if (pattern.kind == Pattern::Kind::Boolean) { require(subject, Type::Bool, pattern.token, "boolean pattern"); return; }
        auto shape_it = shapes_.find(subject.name);
        if (shape_it == shapes_.end() || !shape_it->second->enum_type)
            fail(pattern.token, "variant pattern requires an enum value, got " + subject.name, {}, "E1711");
        const Shape &shape = *shape_it->second;
        std::string variant_name = pattern.value;
        const std::size_t separator = variant_name.rfind("::");
        if (separator != std::string::npos) {
            const std::string qualifier = variant_name.substr(0, separator);
            if (split_type(Type{qualifier}).base != split_type(subject).base)
                fail(pattern.token, "variant pattern '" + pattern.value + "' does not belong to " + subject.name, {}, "E1712");
            variant_name = variant_name.substr(separator + 2);
        }
        const EnumVariant *variant = nullptr;
        for (std::size_t i = 0; i < shape.enum_variants.size(); ++i) if (shape.enum_variants[i].name == variant_name) {
            variant = &shape.enum_variants[i]; pattern.variant_index = i; break;
        }
        if (!variant) fail(pattern.token, "enum '" + subject.name + "' has no variant '" + variant_name + "'", {}, "E1700");
        if (variant->payload.size() != pattern.children.size())
            fail(pattern.token, "variant pattern '" + variant_name + "' expects " + std::to_string(variant->payload.size()) +
                 " payload pattern(s)", {}, "E1713");
        pattern.value = variant_name;
        pattern.payload_types = variant->payload;
        for (std::size_t i = 0; i < pattern.children.size(); ++i) bind_pattern(pattern.children[i], variant->payload[i], bindings);
    }

    static std::string pattern_key(const Pattern &pattern) {
        std::string result = std::to_string(static_cast<int>(pattern.kind)) + ":" + pattern.value + "(";
        for (const Pattern &child : pattern.children) result += pattern_key(child) + ",";
        return result + ")";
    }

    bool patterns_exhaustive(const std::vector<const Pattern *> &patterns, Type subject,
                             std::vector<std::string> *missing = nullptr) const {
        for (const Pattern *pattern : patterns)
            if (pattern->kind == Pattern::Kind::Wildcard || pattern->kind == Pattern::Kind::Binding) return true;
        if (subject == Type::Bool) {
            bool yes = false, no = false;
            for (const Pattern *pattern : patterns) if (pattern->kind == Pattern::Kind::Boolean)
                (pattern->value == "yes" ? yes : no) = true;
            return yes && no;
        }
        auto shape_it = shapes_.find(subject.name);
        if (shape_it == shapes_.end() || !shape_it->second->enum_type) return false;
        const Shape &shape = *shape_it->second;
        bool complete = true;
        for (std::size_t variant_index = 0; variant_index < shape.enum_variants.size(); ++variant_index) {
            std::vector<const Pattern *> variant_patterns;
            for (const Pattern *pattern : patterns)
                if (pattern->kind == Pattern::Kind::Variant && pattern->variant_index == variant_index)
                    variant_patterns.push_back(pattern);
            bool covered = !variant_patterns.empty() && shape.enum_variants[variant_index].payload.empty();
            if (!covered && !variant_patterns.empty()) {
                for (const Pattern *pattern : variant_patterns) {
                    bool irrefutable = true;
                    for (const Pattern &child : pattern->children)
                        irrefutable = irrefutable && (child.kind == Pattern::Kind::Wildcard || child.kind == Pattern::Kind::Binding);
                    if (irrefutable) { covered = true; break; }
                }
                if (!covered && shape.enum_variants[variant_index].payload.size() == 1) {
                    std::vector<const Pattern *> children;
                    for (const Pattern *pattern : variant_patterns) children.push_back(&pattern->children[0]);
                    covered = patterns_exhaustive(children, shape.enum_variants[variant_index].payload[0]);
                }
            }
            if (!covered) {
                complete = false;
                if (missing) missing->push_back(shape.enum_variants[variant_index].name);
            }
        }
        return complete;
    }

    Type match_type(Expr &expression) {
        const Type subject = expression_type(*expression.children[0]);
        const Shape *enum_shape = nullptr;
        if (auto found = shapes_.find(subject.name); found != shapes_.end() && found->second->enum_type) enum_shape = found->second;
        if (!enum_shape && subject != Type::Bool && subject != Type::Int && subject != Type::Str)
            fail(expression.token, "match supports enums, bool, int, and str, got " + subject.name, {}, "E1714");
        std::unordered_set<std::string> covered;
        std::vector<const Pattern *> all_patterns;
        bool catch_all = false;
        Type result = Type::Infer;
        for (std::size_t i = 0; i < expression.match_patterns.size(); ++i) {
            Pattern &pattern = expression.match_patterns[i];
            if (catch_all) fail(pattern.token, "unreachable match arm after wildcard/binding arm", {}, "E1715");
            scopes_.push_back({});
            std::unordered_set<std::string> bindings;
            bind_pattern(pattern, subject, bindings);
            if (pattern.kind == Pattern::Kind::Wildcard || pattern.kind == Pattern::Kind::Binding) catch_all = true;
            else {
                const std::string key = pattern_key(pattern);
                if (!covered.insert(key).second) fail(pattern.token, "unreachable duplicate match arm", {}, "E1715");
            }
            all_patterns.push_back(&pattern);
            const Type arm = expression_type_as(*expression.children[i + 1], expected_type_);
            scopes_.pop_back();
            if (result == Type::Infer) result = arm;
            else require(arm, result, expression.children[i + 1]->token, "match arm");
        }
        if (!catch_all) {
            std::vector<std::string> missing;
            if (!patterns_exhaustive(all_patterns, subject, &missing)) {
                if (enum_shape) {
                    std::string names;
                    for (std::size_t i = 0; i < missing.size(); ++i) names += (i ? ", " : "") + missing[i];
                    fail(expression.token, "non-exhaustive match; missing " + names, "add the missing arm(s) or a wildcard `_`", "E1716");
                }
                if (subject == Type::Bool) fail(expression.token, "non-exhaustive boolean match", "cover true and false or add `_`", "E1716");
                fail(expression.token, "open-ended match requires a wildcard arm", {}, "E1716");
            }
        }
        return result;
    }

    Type propagate_type(Expr &expression) {
        const Type source = expression_type(*expression.children[0]);
        auto source_it = shapes_.find(source.name);
        auto result_it = shapes_.find(current_result_.name);
        if (source_it == shapes_.end() || result_it == shapes_.end() || !source_it->second->enum_type || !result_it->second->enum_type)
            fail(expression.token, "postfix ? requires Option or Result in a function returning the same enum family", {}, "E1720");
        const TypeParts source_parts = split_type(source), result_parts = split_type(current_result_);
        if (source_parts.base != result_parts.base || (source_parts.base != "Option" && source_parts.base != "Result"))
            fail(expression.token, "postfix ? cannot convert " + source.name + " into " + current_result_.name, {}, "E1721");
        if (source_parts.base == "Result" && (source_parts.arguments.size() != 2 || result_parts.arguments.size() != 2 ||
            source_parts.arguments[1] != result_parts.arguments[1]))
            fail(expression.token, "Result propagation requires the same error type", {}, "E1722");
        expression.value = source.name;
        expression.enum_variant = source_parts.base == "Option" ? "Some" : "Ok";
        return source_parts.arguments.at(0);
    }

    Type call_type(Expr &expression) {
        if (const std::size_t separator = expression.value.rfind("::"); separator != std::string::npos) {
            const std::string owner = expression.value.substr(0, separator);
            const std::string variant = expression.value.substr(separator + 2);
            if (enum_templates_.count(split_type(Type{owner}).base))
                return enum_constructor_type(expression, owner, variant);
        }
        if (auto generic = generic_functions_.find(expression.value); generic != generic_functions_.end())
            return specialize_generic_call(expression, *generic->second, 0);
        if (auto generic_shape = generic_shapes_.find(expression.value); generic_shape != generic_shapes_.end()) {
            const Shape &definition = *generic_shape->second;
            std::unordered_map<std::string, Type> bindings;
            if (!expression.type_arguments.empty())
                bindings = type_bindings(definition.generic_parameters, expression.type_arguments, expression.token);
            if (definition.fields.size() != expression.children.size())
                fail(expression.token, "generic struct constructor expects " + std::to_string(definition.fields.size()) + " field value(s)", {}, "E1601");
            Function inference;
            inference.generic_parameters = definition.generic_parameters;
            for (std::size_t i = 0; i < definition.fields.size(); ++i)
                if (!infer_binding(definition.fields[i].type, expression_type(*expression.children[i]), inference, bindings))
                    fail(expression.children[i]->token, "generic struct field conflicts with inferred type arguments", {}, "E1602");
            std::vector<Type> arguments;
            for (const GenericParameter &parameter : definition.generic_parameters) {
                auto found = bindings.find(parameter.name);
                if (found == bindings.end()) fail(expression.token, "cannot infer generic type parameter '" + parameter.name + "'", {}, "E1603");
                arguments.push_back(found->second);
            }
            expression.value = constructed_name(definition.name, arguments);
            expression.type_arguments.clear();
            ensure_concrete_type(Type{expression.value});
        }
        if (expression.value == "cancel" || expression.value == "task_done") {
            if (expression.children.size() != 1)
                fail(expression.token, expression.value + " expects exactly one Task value");
            const Type task = expression_type(*expression.children[0]);
            if (!is_task_type(task))
                fail(expression.children[0]->token, expression.value + " requires a Task value, got " +
                     type_name(task), {}, "E1505");
            return expression.value == "task_done" ? Type::Bool : Type::Void;
        }
        if (expression.value == "task_group_add") {
            if (expression.children.size() != 2)
                fail(expression.token, "task_group_add expects a group handle and one Task value");
            require(expression_type(*expression.children[0]), Type::Int, expression.children[0]->token, "task group handle");
            const Type task = expression_type(*expression.children[1]);
            if (!is_task_type(task))
                fail(expression.children[1]->token, "task_group_add requires a Task value, got " + type_name(task), {}, "E1506");
            return Type::Void;
        }
        if (expression.value == "move" || expression.value == "drop") {
            if (expression.children.size() != 1)
                fail(expression.token, expression.value + " expects exactly one owning binding");
            Expr &argument = *expression.children[0];
            if (argument.kind != Expr::Kind::Variable)
                fail(argument.token, expression.value + " requires a named owning binding",
                     "bind the value to a local variable first", "E0704");
            SemanticVariable *variable = lookup_mut(argument.value);
            if (!variable) {
                const std::string suggestion = name_suggestion(argument.value);
                fail(argument.token, "unknown name '" + argument.value + "'",
                     suggestion.empty() ? std::string{} : "did you mean `" + suggestion + "`?",
                     "E0201", suggestion);
            }
            const bool owning = variable->type == Type::Nums || is_object(variable->type);
            if (!owning)
                fail(argument.token, expression.value + " requires an owning object or nums value, got " +
                     type_name(variable->type), "scalar and value-struct values are copied", "E0704");
            argument.inferred_type = variable->type;
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
                    require(expression_type_as(*expression.children[i], init.parameters[i + 1]), init.parameters[i + 1],
                            expression.children[i]->token, "constructor argument");
                return Type{definition.name};
            }
            normalize_struct_arguments(expression, definition);
            for (size_t i = 0; i < definition.fields.size(); ++i)
                require(expression_type_as(*expression.children[i], definition.fields[i].type), definition.fields[i].type,
                        expression.children[i]->token, "field '" + definition.fields[i].name + "'");
            return Type{definition.name};
        }

        auto signature = signatures_.find(expression.value);
        if (signature == signatures_.end()) {
            const std::string suggestion = name_suggestion(expression.value);
            fail(expression.token, "unknown function '" + expression.value + "'",
                 suggestion.empty() ? std::string{} : "did you mean `" + suggestion + "`?",
                 "E0201", suggestion);
        }
        if (!signature->second.owner_type.empty()) fail(expression.token, "method must be called through an instance");
        if (signature->second.visibility == Visibility::Private && current_function_ &&
            fs::absolute(signature->second.token.file).lexically_normal() !=
                fs::absolute(current_function_->token.file).lexically_normal())
            fail(expression.token, "function '" + expression.value + "' is private to module '" +
                 signature->second.token.file.filename().string() + "'",
                 "mark it `public fn` to export it across module boundaries", "E1304");
        check_call_arguments(expression, signature->second, 0);
        return signature->second.is_async ? task_type(signature->second.result) : signature->second.result;
    }

    Type method_call_type(Expr &expression) {
        if (expression.children.empty()) throw Error("internal error: method call without receiver");
        Type base = expression_type(*expression.children[0]);
        if (is_reference_type(base)) base = pointee_type(base);
        if (!shapes_.count(base.name)) fail(expression.token, "method call requires struct/object receiver, got " + base.name);
        const std::string lowered = base.name + "::" + expression.value;
        if (auto generic = generic_functions_.find(lowered); generic != generic_functions_.end())
            return specialize_generic_call(expression, *generic->second, 1);
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
            const Type expected = signature.parameters[i + self_count];
            const Type actual = expression_type_as(argument, expected);
            if (actual == Type::Void) fail(argument.token, "void cannot be passed as an argument");
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
        result->type_arguments = source.type_arguments;
        result->match_patterns = source.match_patterns;
        result->enum_variant = source.enum_variant;
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
