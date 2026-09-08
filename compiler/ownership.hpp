#ifndef PUNPUN_OWNERSHIP_HPP
#define PUNPUN_OWNERSHIP_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "builtins.hpp"
#include "diagnostics.hpp"
#include "frontend.hpp"

// The type checker answers "is this program well typed?". This pass answers a
// different question: "is each owning value still alive on every path where it
// is used, and do safe borrows obey exclusivity/lifetime rules?" Keeping the
// two passes separate is intentional. It makes control-flow joins explicit and
// gives code generation one authoritative `Expr::consumes_value` bit for moves.
namespace ppownership {

enum class InitState { Initialized, Moved, MaybeMoved };

inline InitState join_state(InitState a, InitState b) {
    if (a == b) return a;
    if (a == InitState::MaybeMoved || b == InitState::MaybeMoved) return InitState::MaybeMoved;
    return InitState::MaybeMoved;
}

inline bool type_needs_drop_impl(const Type &type,
                                 const std::unordered_map<std::string, const Shape *> &shapes,
                                 std::unordered_set<std::string> &visiting) {
    // `nums` is a legacy shared-handle collection in 0.6. Source compatibility
    // requires ordinary assignment and parameter passing to alias it rather than
    // transfer ownership. Explicit move/drop still end a binding lifetime (see
    // consume(..., force=true)); automatic lexical destruction therefore applies
    // only to genuinely owned values such as objects and owning aggregate fields.
    if (type == Type::Nums) return false;
    if (type == Type::Int || type == Type::Float || type == Type::Bool || type == Type::Str ||
        type == Type::Void || type == Type::Infer || is_pointer_like_type(type) ||
        is_slice_type(type) || is_task_type(type)) return false;
    auto found = shapes.find(type.name);
    if (found == shapes.end()) return false;
    const Shape &shape = *found->second;
    if (shape.reference_type) return true;
    if (!visiting.insert(shape.name).second) return false;
    bool result = false;
    if (shape.enum_type) {
        for (const EnumVariant &variant : shape.enum_variants)
            for (const Type &payload : variant.payload)
                if (type_needs_drop_impl(payload, shapes, visiting)) { result = true; break; }
    } else {
        for (const Parameter &field : shape.fields)
            if (type_needs_drop_impl(field.type, shapes, visiting)) { result = true; break; }
    }
    visiting.erase(shape.name);
    return result;
}

inline bool type_needs_drop(const Type &type,
                            const std::unordered_map<std::string, const Shape *> &shapes) {
    std::unordered_set<std::string> visiting;
    return type_needs_drop_impl(type, shapes, visiting);
}

inline bool is_copy_type(const Type &type,
                         const std::unordered_map<std::string, const Shape *> &shapes) {
    return !type_needs_drop(type, shapes);
}

class Analyzer {
  public:
    explicit Analyzer(std::vector<Module> &modules) : modules_(modules) {}

    void analyze() {
        collect_program();
        for (Module &module : modules_)
            for (Function &function : module.functions)
                if (!function.is_extern_native) analyze_function(function);
    }

  private:
    struct Signature {
        std::vector<Type> parameters;
        bool is_method = false;
        bool self_mutable = false;
        bool is_async = false;
    };

    struct Binding {
        std::size_t id = 0;
        Type type = Type::Infer;
        bool mutable_value = false;
        bool parameter = false;
        std::size_t scope_depth = 0;
        Token token{TokenKind::End, "", "<ownership>", 1, 1, 0, 0};
        InitState state = InitState::Initialized;
        std::unordered_set<std::size_t> borrow_owners;
        bool mutable_borrow = false;
    };

    using Scope = std::unordered_map<std::string, Binding>;

    std::vector<Module> &modules_;
    std::unordered_map<std::string, const Shape *> shapes_;
    std::unordered_map<std::string, Signature> signatures_;
    std::unordered_map<std::string, const BuiltinSpec *> builtins_;
    std::vector<Scope> scopes_;
    std::size_t next_binding_id_ = 1;
    const Function *current_function_ = nullptr;

    [[noreturn]] static void fail(const Token &token, const std::string &message,
                                  const std::string &help = {}, const std::string &code = "E0700") {
        throw Error(ppdiag::format(token.file, token.line, token.column, token.length,
                                   code, message, message, help));
    }

    void collect_program() {
        for (const BuiltinSpec &builtin : punpun_builtins()) builtins_[builtin.name] = &builtin;
        for (const Module &module : modules_) {
            for (const Shape &shape : module.shapes) shapes_[shape.name] = &shape;
            for (const Function &function : module.functions) {
                Signature signature;
                signature.is_method = function.is_method;
                signature.self_mutable = function.self_mutable;
                signature.is_async = function.is_async;
                for (const Parameter &parameter : function.parameters) signature.parameters.push_back(parameter.type);
                signatures_[function.name] = std::move(signature);
            }
        }
    }

    Binding *lookup_mut(const std::string &name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    const Binding *lookup(const std::string &name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    Binding *find_by_id(std::size_t id) {
        for (Scope &scope : scopes_)
            for (auto &[name, binding] : scope) {
                (void)name;
                if (binding.id == id) return &binding;
            }
        return nullptr;
    }

    struct BorrowSummary { bool shared = false; bool mut = false; };

    BorrowSummary active_borrows(std::size_t owner_id) const {
        BorrowSummary result;
        for (const Scope &scope : scopes_)
            for (const auto &[name, binding] : scope) {
                (void)name;
                if (binding.state == InitState::Moved || !binding.borrow_owners.count(owner_id)) continue;
                if (binding.mutable_borrow) result.mut = true;
                else result.shared = true;
            }
        return result;
    }

    void require_initialized(const Binding &binding, const Token &token) const {
        if (binding.state == InitState::Moved)
            fail(token, "use of moved value '" + binding_name(binding.id) + "'",
                 "assign a new value before using this binding again", "E0703");
        if (binding.state == InitState::MaybeMoved)
            fail(token, "value '" + binding_name(binding.id) + "' may have been moved on another control-flow path",
                 "reinitialize it on every path before this use", "E0706");
    }

    std::string binding_name(std::size_t id) const {
        for (const Scope &scope : scopes_)
            for (const auto &[name, binding] : scope) if (binding.id == id) return name;
        return "<binding>";
    }

    void check_read(Binding &binding, const Token &token) {
        require_initialized(binding, token);
        const BorrowSummary borrows = active_borrows(binding.id);
        if (borrows.mut)
            fail(token, "cannot use '" + binding_name(binding.id) + "' while it is mutably borrowed",
                 "use the mutable reference until its last use or move the borrow into a shorter scope", "E0707");
    }

    void check_borrow(Binding &owner, bool mut, const Token &token) {
        require_initialized(owner, token);
        const BorrowSummary borrows = active_borrows(owner.id);
        if (mut && (borrows.shared || borrows.mut))
            fail(token, "cannot mutably borrow '" + binding_name(owner.id) + "' because another borrow is live",
                 "end the existing borrow before creating an exclusive borrow", "E0707");
        if (!mut && borrows.mut)
            fail(token, "cannot borrow '" + binding_name(owner.id) + "' while an exclusive borrow is live",
                 "end the mutable borrow before creating a shared borrow", "E0707");
    }

    void check_mutation(Binding &owner, const Token &token) {
        require_initialized(owner, token);
        const BorrowSummary borrows = active_borrows(owner.id);
        if (borrows.shared || borrows.mut)
            fail(token, "cannot mutate '" + binding_name(owner.id) + "' while it is borrowed",
                 "end the borrow before mutating the owner", "E0707");
    }

    Binding *root_binding(Expr &expression) {
        if (expression.kind == Expr::Kind::Variable) {
            Binding *binding = lookup_mut(expression.value);
            if (binding) expression.binding_id = binding->id;
            return binding;
        }
        if (expression.kind == Expr::Kind::Member && !expression.children.empty())
            return root_binding(*expression.children[0]);
        return nullptr;
    }

    std::unordered_set<std::size_t> borrow_sources(Expr &expression, bool *mutable_borrow = nullptr) {
        if (mutable_borrow) *mutable_borrow = false;
        if (expression.kind == Expr::Kind::Unary &&
            (expression.value == "&" || expression.value == "&mut")) {
            if (Binding *owner = root_binding(*expression.children[0])) {
                if (mutable_borrow) *mutable_borrow = expression.value == "&mut";
                return {owner->id};
            }
        }
        if (expression.kind == Expr::Kind::Call && expression.value == "view" && !expression.children.empty()) {
            if (Binding *owner = root_binding(*expression.children[0])) return {owner->id};
        }
        if (expression.kind == Expr::Kind::Variable) {
            if (Binding *source = lookup_mut(expression.value)) {
                if (mutable_borrow) *mutable_borrow = source->mutable_borrow;
                return source->borrow_owners;
            }
        }
        return {};
    }

    void set_borrow_relation(Binding &target, Expr &source) {
        bool mut = false;
        std::unordered_set<std::size_t> owners = borrow_sources(source, &mut);
        for (std::size_t owner_id : owners) {
            Binding *owner = find_by_id(owner_id);
            if (owner && owner->scope_depth > target.scope_depth)
                fail(source.token, "borrow would outlive its owner '" + binding_name(owner_id) + "'",
                     "keep the reference/slice in the owner's scope or move the owned value instead", "E0708");
        }
        target.borrow_owners = std::move(owners);
        target.mutable_borrow = mut;
    }

    void consume(Expr &expression, const Token &token, bool force = false) {
        if (!force && !type_needs_drop(expression.inferred_type, shapes_)) return;
        if (expression.kind == Expr::Kind::Variable) {
            Binding *binding = lookup_mut(expression.value);
            if (!binding) fail(expression.token, "unknown owning binding '" + expression.value + "'", {}, "E0201");
            require_initialized(*binding, token);
            const BorrowSummary borrows = active_borrows(binding->id);
            if (borrows.shared || borrows.mut)
                fail(token, "cannot move '" + expression.value + "' while it is borrowed",
                     "end the borrow before transferring ownership", "E0707");
            binding->state = InitState::Moved;
            expression.consumes_value = true;
            return;
        }
        if (expression.kind == Expr::Kind::Member || expression.kind == Expr::Kind::Index)
            fail(token, "partial moves from fields and indexed elements are not supported in safe PunPun",
                 "move the whole owning value instead", "E0709");
        // A temporary owning expression is already uniquely owned by its consumer.
    }

    void analyze_function(Function &function) {
        current_function_ = &function;
        scopes_.clear();
        scopes_.push_back({});
        for (Parameter &parameter : function.parameters) {
            Binding binding;
            binding.id = next_binding_id_++;
            binding.type = parameter.type;
            binding.mutable_value = parameter.mutable_value;
            binding.parameter = true;
            binding.scope_depth = 0;
            binding.token = parameter.token;
            parameter.binding_id = binding.id;
            scopes_.back()[parameter.name] = std::move(binding);
        }
        for (Stmt &statement : function.body) analyze_statement(statement);
        scopes_.clear();
        current_function_ = nullptr;
    }

    void analyze_nested_block(std::vector<Stmt> &statements) {
        scopes_.push_back({});
        for (Stmt &statement : statements) analyze_statement(statement);
        scopes_.pop_back();
    }

    std::vector<Scope> analyze_branch(const std::vector<Scope> &base, std::vector<Stmt> &statements) {
        scopes_ = base;
        analyze_nested_block(statements);
        return scopes_;
    }

    static InitState joined(const std::vector<InitState> &states) {
        InitState result = states.front();
        for (std::size_t i = 1; i < states.size(); ++i) result = join_state(result, states[i]);
        return result;
    }

    void merge_paths(const std::vector<Scope> &base, const std::vector<std::vector<Scope>> &paths) {
        scopes_ = base;
        for (std::size_t depth = 0; depth < base.size(); ++depth) {
            for (auto &[name, merged_binding] : scopes_[depth]) {
                std::vector<InitState> states;
                std::unordered_set<std::size_t> owners;
                bool mutable_borrow = false;
                for (const auto &path : paths) {
                    auto found = path[depth].find(name);
                    if (found == path[depth].end() || found->second.id != merged_binding.id) continue;
                    states.push_back(found->second.state);
                    owners.insert(found->second.borrow_owners.begin(), found->second.borrow_owners.end());
                    mutable_borrow = mutable_borrow || found->second.mutable_borrow;
                }
                if (!states.empty()) merged_binding.state = joined(states);
                merged_binding.borrow_owners = std::move(owners);
                merged_binding.mutable_borrow = mutable_borrow;
            }
        }
    }

    void analyze_statement(Stmt &statement) {
        switch (statement.kind) {
            case Stmt::Kind::Variable: {
                analyze_expression(*statement.expression);
                consume(*statement.expression, statement.expression->token);
                Binding binding;
                binding.id = next_binding_id_++;
                binding.type = statement.declared_type == Type::Infer ? statement.expression->inferred_type : statement.declared_type;
                binding.mutable_value = statement.mutable_value;
                binding.scope_depth = scopes_.size() - 1;
                binding.token = statement.token;
                set_borrow_relation(binding, *statement.expression);
                statement.binding_id = binding.id;
                scopes_.back()[statement.name] = std::move(binding);
                return;
            }
            case Stmt::Kind::Assign: {
                if (statement.assignment_op != "=" && statement.assignment_op != "<-") {
                    analyze_expression(*statement.target);
                    analyze_expression(*statement.expression);
                    return;
                }
                analyze_expression(*statement.expression);
                consume(*statement.expression, statement.expression->token);
                if (statement.target->kind == Expr::Kind::Variable) {
                    Binding *target = lookup_mut(statement.target->value);
                    if (target) statement.target->binding_id = target->id;
                    if (!target) fail(statement.target->token, "unknown binding '" + statement.target->value + "'", {}, "E0201");
                    // Whole-value assignment is the one operation allowed to reinitialize a moved binding.
                    if (target->state != InitState::Moved && target->state != InitState::MaybeMoved)
                        check_mutation(*target, statement.target->token);
                    else {
                        const BorrowSummary borrows = active_borrows(target->id);
                        if (borrows.shared || borrows.mut)
                            fail(statement.target->token, "cannot reinitialize borrowed binding '" + statement.target->value + "'", {}, "E0707");
                    }
                    target->state = InitState::Initialized;
                    set_borrow_relation(*target, *statement.expression);
                } else {
                    if (Binding *owner = root_binding(*statement.target)) check_mutation(*owner, statement.target->token);
                    analyze_expression(*statement.target);
                }
                return;
            }
            case Stmt::Kind::Expression: analyze_expression(*statement.expression); return;
            case Stmt::Kind::Say: analyze_expression(*statement.expression); return;
            case Stmt::Kind::Return: {
                if (!statement.expression) return;
                analyze_expression(*statement.expression);
                if (is_reference_type(statement.expression->inferred_type) || is_slice_type(statement.expression->inferred_type)) {
                    const auto owners = borrow_sources(*statement.expression);
                    if (!owners.empty())
                        fail(statement.expression->token, "borrowed value cannot escape this function",
                             "return an existing reference parameter or return/move the owned value instead", "E0702");
                }
                consume(*statement.expression, statement.expression->token);
                return;
            }
            case Stmt::Kind::If: {
                analyze_expression(*statement.expression);
                const std::vector<Scope> base = scopes_;
                std::vector<std::vector<Scope>> paths;
                paths.push_back(analyze_branch(base, statement.body));
                if (statement.alternative.empty()) paths.push_back(base);
                else paths.push_back(analyze_branch(base, statement.alternative));
                merge_paths(base, paths);
                return;
            }
            case Stmt::Kind::While: {
                analyze_expression(*statement.expression);
                const std::vector<Scope> base = scopes_;
                std::vector<Scope> body = analyze_branch(base, statement.body);
                merge_paths(base, {base, body});
                return;
            }
            case Stmt::Kind::Each: {
                analyze_expression(*statement.expression);
                analyze_expression(*statement.upper);
                const std::vector<Scope> base = scopes_;
                scopes_ = base;
                scopes_.push_back({});
                Binding counter;
                counter.id = next_binding_id_++;
                counter.type = Type::Int;
                counter.scope_depth = scopes_.size() - 1;
                counter.token = statement.token;
                statement.binding_id = counter.id;
                scopes_.back()[statement.name] = std::move(counter);
                for (Stmt &child : statement.body) analyze_statement(child);
                scopes_.pop_back();
                const std::vector<Scope> body = scopes_;
                merge_paths(base, {base, body});
                return;
            }
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue: return;
            case Stmt::Kind::Unsafe: analyze_nested_block(statement.body); return;
        }
    }

    void analyze_expression(Expr &expression) {
        switch (expression.kind) {
            case Expr::Kind::Integer:
            case Expr::Kind::Float:
            case Expr::Kind::String:
            case Expr::Kind::Boolean:
            case Expr::Kind::SizeOf:
            case Expr::Kind::AlignOf: return;
            case Expr::Kind::Variable: {
                Binding *binding = lookup_mut(expression.value);
                if (!binding) return; // enum constructors and compiler-generated names are not locals.
                expression.binding_id = binding->id;
                check_read(*binding, expression.token);
                return;
            }
            case Expr::Kind::Member:
            case Expr::Kind::Index:
            case Expr::Kind::List:
                for (auto &child : expression.children) analyze_expression(*child);
                return;
            case Expr::Kind::Unary: {
                if (expression.value == "&" || expression.value == "&mut") {
                    Binding *owner = root_binding(*expression.children[0]);
                    if (owner) check_borrow(*owner, expression.value == "&mut", expression.token);
                    return;
                }
                analyze_expression(*expression.children[0]);
                return;
            }
            case Expr::Kind::Binary:
                analyze_expression(*expression.children[0]);
                analyze_expression(*expression.children[1]);
                return;
            case Expr::Kind::EnumConstruct: {
                const Shape *shape = shapes_.at(expression.value);
                const EnumVariant *variant = nullptr;
                for (const EnumVariant &candidate : shape->enum_variants)
                    if (candidate.name == expression.enum_variant) { variant = &candidate; break; }
                if (!variant) fail(expression.token, "internal ownership error: missing enum variant");
                for (std::size_t i = 0; i < expression.children.size(); ++i) {
                    analyze_expression(*expression.children[i]);
                    consume(*expression.children[i], expression.children[i]->token);
                }
                return;
            }
            case Expr::Kind::Match: analyze_match(expression); return;
            case Expr::Kind::Propagate:
                analyze_expression(*expression.children[0]);
                consume(*expression.children[0], expression.children[0]->token);
                return;
            case Expr::Kind::Call:
            case Expr::Kind::MethodCall: analyze_call(expression); return;
        }
    }

    void bind_pattern(Pattern &pattern, Type subject) {
        if (pattern.kind == Pattern::Kind::Binding) {
            Binding binding;
            binding.id = next_binding_id_++;
            binding.type = subject;
            binding.scope_depth = scopes_.size() - 1;
            binding.token = pattern.token;
            pattern.binding_id = binding.id;
            scopes_.back()[pattern.value] = std::move(binding);
            return;
        }
        if (pattern.kind != Pattern::Kind::Variant) return;
        auto shape = shapes_.find(subject.name);
        if (shape == shapes_.end() || pattern.variant_index >= shape->second->enum_variants.size()) return;
        const EnumVariant &variant = shape->second->enum_variants[pattern.variant_index];
        for (std::size_t i = 0; i < pattern.children.size() && i < variant.payload.size(); ++i)
            bind_pattern(pattern.children[i], variant.payload[i]);
    }

    void analyze_match(Expr &expression) {
        analyze_expression(*expression.children[0]);
        consume(*expression.children[0], expression.children[0]->token);
        const std::vector<Scope> base = scopes_;
        std::vector<std::vector<Scope>> paths;
        for (std::size_t i = 0; i < expression.match_patterns.size(); ++i) {
            scopes_ = base;
            scopes_.push_back({});
            bind_pattern(expression.match_patterns[i], expression.children[0]->inferred_type);
            analyze_expression(*expression.children[i + 1]);
            scopes_.pop_back();
            paths.push_back(scopes_);
        }
        if (!paths.empty()) merge_paths(base, paths);
    }

    void analyze_call(Expr &expression) {
        if (expression.kind == Expr::Kind::Call && (expression.value == "move" || expression.value == "drop")) {
            if (expression.children.empty()) return;
            Expr &argument = *expression.children[0];
            if (argument.kind == Expr::Kind::Variable) {
                Binding *binding = lookup_mut(argument.value);
                if (binding) {
                    argument.binding_id = binding->id;
                    check_read(*binding, argument.token);
                }
            }
            consume(argument, argument.token, true);
            return;
        }

        // Runtime list APIs borrow their nums argument. They intentionally do
        // not use ordinary by-value ownership semantics.
        static const std::unordered_set<std::string> nums_shared{"at", "size", "view"};
        static const std::unordered_set<std::string> nums_mut{"push", "put", "pop", "sort"};
        if (expression.kind == Expr::Kind::Call && !expression.children.empty() &&
            (nums_shared.count(expression.value) || nums_mut.count(expression.value))) {
            Expr &owner_expr = *expression.children[0];
            Binding *owner = root_binding(owner_expr);
            if (owner) {
                if (nums_mut.count(expression.value)) check_mutation(*owner, owner_expr.token);
                else check_borrow(*owner, false, owner_expr.token);
                require_initialized(*owner, owner_expr.token);
            } else analyze_expression(owner_expr);
            for (std::size_t i = 1; i < expression.children.size(); ++i) analyze_expression(*expression.children[i]);
            return;
        }

        if (expression.kind == Expr::Kind::MethodCall) {
            auto signature = signatures_.find(expression.value);
            if (signature == signatures_.end()) {
                for (auto &child : expression.children) analyze_expression(*child);
                return;
            }
            if (!expression.children.empty()) {
                Expr &receiver = *expression.children[0];
                Binding *owner = root_binding(receiver);
                if (owner) {
                    if (signature->second.self_mutable) check_mutation(*owner, receiver.token);
                    else check_borrow(*owner, false, receiver.token);
                    require_initialized(*owner, receiver.token);
                } else analyze_expression(receiver);
            }
            for (std::size_t i = 1; i < expression.children.size(); ++i) {
                analyze_expression(*expression.children[i]);
                if (i < signature->second.parameters.size()) consume(*expression.children[i], expression.children[i]->token);
            }
            return;
        }

        if (auto shape = shapes_.find(expression.value); shape != shapes_.end()) {
            const Shape &definition = *shape->second;
            std::vector<Type> parameters;
            if (definition.reference_type && !definition.initializer_name.empty()) {
                auto init = signatures_.find(definition.initializer_name);
                if (init != signatures_.end() && init->second.parameters.size() > 1)
                    parameters.assign(init->second.parameters.begin() + 1, init->second.parameters.end());
            } else {
                for (const Parameter &field : definition.fields) parameters.push_back(field.type);
            }
            for (std::size_t i = 0; i < expression.children.size(); ++i) {
                analyze_expression(*expression.children[i]);
                if (i < parameters.size()) consume(*expression.children[i], expression.children[i]->token);
            }
            return;
        }

        auto builtin = builtins_.find(expression.value);
        if (builtin != builtins_.end()) {
            for (std::size_t i = 0; i < expression.children.size(); ++i) analyze_expression(*expression.children[i]);
            return; // ordinary builtins do not take ownership in 0.6.
        }

        auto signature = signatures_.find(expression.value);
        if (signature == signatures_.end()) {
            for (auto &child : expression.children) analyze_expression(*child);
            return;
        }
        for (std::size_t i = 0; i < expression.children.size(); ++i) {
            analyze_expression(*expression.children[i]);
            if (i < signature->second.parameters.size()) consume(*expression.children[i], expression.children[i]->token);
        }
    }
};

} // namespace ppownership

#endif
