#ifndef PUNPUN_HIR_HPP
#define PUNPUN_HIR_HPP

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "frontend.hpp"
#include "ownership.hpp"

namespace pphir {

using ValueId = std::size_t;
using BlockId = std::size_t;
constexpr ValueId NoValue = static_cast<ValueId>(-1);
constexpr BlockId NoBlock = static_cast<BlockId>(-1);

enum class Op {
    Parameter, Constant, Load, MoveLoad, Store, Drop, Call, Construct, EnumConstruct, Match, Propagate, Await, Unary, Binary, Member, Index, List,
    AddressOf, Deref, SizeOf, AlignOf, StoreMember, StoreIndex, StoreIndirect, Say
};

struct Instruction {
    Op op;
    ValueId result = NoValue;
    Type type = Type::Void;
    std::string detail;
    std::vector<ValueId> operands;
    Token token;
};

struct Terminator {
    enum class Kind { None, Jump, Branch, Return, Unreachable } kind = Kind::None;
    ValueId value = NoValue;
    BlockId first = NoBlock;
    BlockId second = NoBlock;
    Token token{TokenKind::End, "", "<hir>", 1, 1, 0, 0};
};

struct Block {
    BlockId id = 0;
    std::vector<Instruction> instructions;
    Terminator terminator;
};

struct Function {
    std::string name;
    std::string native_symbol;
    bool external_native = false;
    bool is_async = false;
    Type result = Type::Void;
    std::vector<std::pair<std::string, Type>> parameters;
    std::vector<Block> blocks;
    std::size_t value_count = 0;
};

struct Program { std::vector<Function> functions; };

inline const char *op_name(Op op) {
    switch (op) {
        case Op::Parameter: return "param";
        case Op::Constant: return "const";
        case Op::Load: return "load";
        case Op::MoveLoad: return "move.load";
        case Op::Store: return "store";
        case Op::Drop: return "drop";
        case Op::Call: return "call";
        case Op::Construct: return "construct";
        case Op::EnumConstruct: return "enum.construct";
        case Op::Match: return "match";
        case Op::Propagate: return "propagate";
        case Op::Await: return "await";
        case Op::Unary: return "unary";
        case Op::Binary: return "binary";
        case Op::Member: return "member";
        case Op::Index: return "index";
        case Op::List: return "list";
        case Op::AddressOf: return "address.of";
        case Op::Deref: return "deref";
        case Op::SizeOf: return "sizeof";
        case Op::AlignOf: return "alignof";
        case Op::StoreMember: return "store.member";
        case Op::StoreIndex: return "store.index";
        case Op::StoreIndirect: return "store.indirect";
        case Op::Say: return "say";
    }
    return "?";
}

class Lowerer {
  public:
    explicit Lowerer(const std::vector<Module> &modules) : modules_(modules) {
        for (const Module &module : modules_)
            for (const Shape &shape : module.shapes) {
                shape_names_.insert(shape.name);
                shapes_[shape.name] = &shape;
            }
    }

    Program lower() {
        Program program;
        for (const Module &module : modules_)
            for (const ::Function &function : module.functions)
                program.functions.push_back(lower_function(function));
        verify(program);
        return program;
    }

    static void verify(const Program &program) {
        std::unordered_set<std::string> names;
        for (const Function &function : program.functions) {
            if (!names.insert(function.name).second)
                throw Error("internal compiler error: duplicate HIR function '" + function.name + "'");
            if (function.external_native) {
                if (!function.blocks.empty()) throw Error("internal compiler error: extern HIR function has a body: " + function.name);
                continue;
            }
            if (function.blocks.empty())
                throw Error("internal compiler error: HIR function has no entry block: " + function.name);

            // Value numbering is function-global. CFG construction is allowed to
            // create a merge/test block before the block that defines a value on
            // one of its incoming paths, so numeric block order is not a valid
            // dominance test. First collect every unique definition, then validate
            // references. Definite-assignment semantics for lexical storage are
            // enforced by the semantic/ownership passes rather than by block IDs.
            std::vector<bool> values(function.value_count, false);
            for (std::size_t expected = 0; expected < function.blocks.size(); ++expected) {
                const Block &block = function.blocks[expected];
                if (block.id != expected)
                    throw Error("internal compiler error: non-canonical HIR block numbering");
                for (const Instruction &instruction : block.instructions) {
                    if (instruction.result == NoValue) continue;
                    if (instruction.result >= values.size() || values[instruction.result])
                        throw Error("internal compiler error: duplicate/out-of-range HIR value");
                    if (instruction.type == Type::Void)
                        throw Error("internal compiler error: void HIR instruction defines a value");
                    values[instruction.result] = true;
                }
            }
            for (const Block &block : function.blocks) {
                for (const Instruction &instruction : block.instructions) {
                    for (ValueId operand : instruction.operands)
                        check_value(operand, values);
                }
                const Terminator &term = block.terminator;
                if (term.kind == Terminator::Kind::None)
                    throw Error("internal compiler error: unterminated HIR block");
                if (term.kind == Terminator::Kind::Jump) check_block(term.first, function.blocks.size());
                if (term.kind == Terminator::Kind::Branch) {
                    check_value(term.value, values);
                    check_block(term.first, function.blocks.size());
                    check_block(term.second, function.blocks.size());
                }
                if (term.kind == Terminator::Kind::Return && term.value != NoValue)
                    check_value(term.value, values);
            }
        }
    }

  private:
    struct DropBinding {
        std::string storage;
        Type type = Type::Void;
        Token token{TokenKind::End, "", "<hir>", 1, 1, 0, 0};
    };
    struct LoopInfo {
        BlockId continue_block = NoBlock;
        BlockId break_block = NoBlock;
        std::size_t scope_base = 0;
    };
    struct PendingPatternBinding {
        const Pattern *pattern = nullptr;
        ValueId value = NoValue;
        Type type = Type::Void;
    };

    const std::vector<Module> &modules_;
    std::unordered_set<std::string> shape_names_;
    std::unordered_map<std::string, const Shape *> shapes_;
    Function *function_ = nullptr;
    const ::Function *source_function_ = nullptr;
    BlockId current_ = 0;
    ValueId next_value_ = 0;
    std::size_t next_temp_storage_ = 0;
    std::vector<LoopInfo> loops_;
    std::vector<std::vector<DropBinding>> drop_scopes_;

    static void check_block(BlockId block, std::size_t count) {
        if (block == NoBlock || block >= count)
            throw Error("internal compiler error: HIR terminator targets invalid block");
    }
    static void check_value(ValueId value, const std::vector<bool> &defined) {
        if (value == NoValue || value >= defined.size() || !defined[value])
            throw Error("internal compiler error: HIR terminator uses undefined value");
    }

    static std::string storage_name(std::size_t binding_id, const std::string &source_name) {
        if (binding_id == 0)
            throw Error("internal compiler error: lexical binding '" + source_name + "' has no canonical ownership identity");
        return "$b" + std::to_string(binding_id) + ":" + source_name;
    }

    std::string temporary_storage(const std::string &purpose) {
        return "$tmp." + purpose + "." + std::to_string(next_temp_storage_++);
    }

    bool needs_drop(const Type &type) const { return ppownership::type_needs_drop(type, shapes_); }

    const Shape &shape(const Type &type) const {
        auto found = shapes_.find(type.name);
        if (found == shapes_.end())
            throw Error("internal compiler error: HIR requires missing shape metadata for '" + type.name + "'");
        return *found->second;
    }

    static std::size_t variant_index(const Shape &shape, const std::string &name) {
        for (std::size_t i = 0; i < shape.enum_variants.size(); ++i)
            if (shape.enum_variants[i].name == name) return i;
        throw Error("internal compiler error: HIR requires unknown enum variant '" + name + "'");
    }

    Function lower_function(const ::Function &source) {
        Function function;
        function.name = source.name;
        function.result = source.result;
        function.external_native = source.is_extern_native;
        function.is_async = source.is_async;
        function.native_symbol = source.native_symbol;
        if (source.is_extern_native) {
            for (const Parameter &parameter : source.parameters) function.parameters.push_back({parameter.name, parameter.type});
            return function;
        }
        function_ = &function;
        source_function_ = &source;
        current_ = create_block();
        next_value_ = 0;
        next_temp_storage_ = 0;
        loops_.clear();
        drop_scopes_.clear();
        drop_scopes_.push_back({});

        for (std::size_t parameter_index = 0; parameter_index < source.parameters.size(); ++parameter_index) {
            const Parameter &parameter = source.parameters[parameter_index];
            function.parameters.push_back({parameter.name, parameter.type});
            const std::string storage = storage_name(parameter.binding_id, parameter.name);
            const ValueId value = emit_value(Op::Parameter, parameter.type, storage, {}, parameter.token);
            emit_effect(Op::Store, storage, {value}, parameter.token);
            // Method `self` is a borrowed receiver even though its lowered type
            // is the object identity type. The caller owns it; the callee must
            // never schedule a lexical drop for the receiver.
            if (!(source.is_method && parameter_index == 0))
                register_drop(storage, parameter.type, parameter.token);
        }

        lower_statements(source.body);
        if (!terminated()) {
            emit_drops_from(0);
            if (source.name == "main") {
                const ValueId zero = emit_value(Op::Constant, Type::Int, "0", {}, source.token);
                terminate_return(zero, source.token);
            } else if (source.result == Type::Void) {
                terminate_return(NoValue, source.token);
            } else {
                throw Error("internal compiler error: non-void function reached HIR fallthrough after semantic analysis");
            }
        }
        function.value_count = next_value_;
        drop_scopes_.clear();
        source_function_ = nullptr;
        function_ = nullptr;
        return function;
    }

    BlockId create_block() {
        const BlockId id = function_->blocks.size();
        function_->blocks.push_back(Block{id, {}, {}});
        return id;
    }

    Block &block() { return function_->blocks[current_]; }
    bool terminated() const { return function_->blocks[current_].terminator.kind != Terminator::Kind::None; }

    ValueId emit_value(Op op, Type type, std::string detail, std::vector<ValueId> operands, const Token &token) {
        if (type == Type::Void) throw Error("internal compiler error: attempted to materialize void HIR value");
        const ValueId id = next_value_++;
        block().instructions.push_back({op, id, type, std::move(detail), std::move(operands), token});
        return id;
    }

    void emit_effect(Op op, std::string detail, std::vector<ValueId> operands, const Token &token) {
        block().instructions.push_back({op, NoValue, Type::Void, std::move(detail), std::move(operands), token});
    }

    void emit_typed_effect(Op op, Type type, std::string detail, std::vector<ValueId> operands, const Token &token) {
        block().instructions.push_back({op, NoValue, std::move(type), std::move(detail), std::move(operands), token});
    }

    void terminate_jump(BlockId target, const Token &token) {
        block().terminator = {Terminator::Kind::Jump, NoValue, target, NoBlock, token};
    }
    void terminate_branch(ValueId condition, BlockId yes, BlockId no, const Token &token) {
        block().terminator = {Terminator::Kind::Branch, condition, yes, no, token};
    }
    void terminate_return(ValueId value, const Token &token) {
        block().terminator = {Terminator::Kind::Return, value, NoBlock, NoBlock, token};
    }
    void terminate_unreachable(const Token &token) {
        block().terminator = {Terminator::Kind::Unreachable, NoValue, NoBlock, NoBlock, token};
    }

    void register_drop(const std::string &storage, Type type, const Token &token) {
        if (!needs_drop(type)) return;
        if (drop_scopes_.empty())
            throw Error("internal compiler error: owning HIR binding registered without lexical scope");
        drop_scopes_.back().push_back({storage, std::move(type), token});
    }

    void emit_drop(const DropBinding &binding) {
        emit_typed_effect(Op::Drop, binding.type, binding.storage, {}, binding.token);
    }

    void emit_scope_drops(std::size_t depth) {
        if (depth >= drop_scopes_.size()) return;
        for (auto it = drop_scopes_[depth].rbegin(); it != drop_scopes_[depth].rend(); ++it)
            emit_drop(*it);
    }

    void emit_drops_from(std::size_t first_depth) {
        for (std::size_t depth = drop_scopes_.size(); depth-- > first_depth;)
            emit_scope_drops(depth);
    }

    void lower_block(const std::vector<Stmt> &statements) {
        drop_scopes_.push_back({});
        lower_statements(statements);
        if (!terminated()) emit_scope_drops(drop_scopes_.size() - 1);
        drop_scopes_.pop_back();
    }

    void lower_statements(const std::vector<Stmt> &statements) {
        for (const Stmt &statement : statements) {
            if (terminated()) break;
            lower_statement(statement);
        }
    }

    void lower_statement(const Stmt &statement) {
        switch (statement.kind) {
            case Stmt::Kind::Variable: {
                const ValueId value = lower_expression(*statement.expression);
                const Type type = statement.declared_type == Type::Infer ? statement.expression->inferred_type : statement.declared_type;
                const std::string storage = storage_name(statement.binding_id, statement.name);
                emit_effect(Op::Store, storage, {value}, statement.token);
                register_drop(storage, type, statement.token);
                return;
            }
            case Stmt::Kind::Assign: {
                const Expr &target = *statement.target;

                // PunPun assignment evaluation is left-to-right: resolve the
                // lvalue (including member base, index, or pointer) before the
                // right-hand expression. This matters when either side calls a
                // function with visible effects.
                ValueId lvalue_base = NoValue;
                ValueId lvalue_index = NoValue;
                ValueId lvalue_pointer = NoValue;
                ValueId current = NoValue;
                if (target.kind == Expr::Kind::Member) {
                    const Expr &base_expr = *target.children[0];
                    const auto shape_it = shapes_.find(base_expr.inferred_type.name);
                    const bool object_identity = shape_it != shapes_.end() && shape_it->second->reference_type;
                    if (object_identity || is_pointer_like_type(base_expr.inferred_type))
                        lvalue_base = lower_expression(base_expr);
                    else
                        lvalue_base = lower_address(base_expr, Type{"&mut " + base_expr.inferred_type.name}, "&mut", target.token);
                } else if (target.kind == Expr::Kind::Index) {
                    lvalue_base = lower_expression(*target.children[0]);
                    lvalue_index = lower_expression(*target.children[1]);
                } else if (target.kind == Expr::Kind::Unary && target.value == "*") {
                    lvalue_pointer = lower_expression(*target.children[0]);
                } else if (target.kind == Expr::Kind::Variable &&
                           statement.assignment_op != "=" && statement.assignment_op != "<-") {
                    current = lower_expression(target);
                }

                ValueId value = lower_expression(*statement.expression);
                if (statement.assignment_op != "=" && statement.assignment_op != "<-") {
                    if (current == NoValue) current = lower_expression(target);
                    const std::string op(1, statement.assignment_op.front());
                    value = emit_value(Op::Binary, target.inferred_type, op, {current, value}, statement.token);
                }
                if (target.kind == Expr::Kind::Variable) {
                    const std::string storage = storage_name(target.binding_id, target.value);
                    if (statement.assignment_op == "=" || statement.assignment_op == "<-") {
                        if (needs_drop(target.inferred_type))
                            emit_typed_effect(Op::Drop, target.inferred_type, storage, {}, target.token);
                    }
                    emit_effect(Op::Store, storage, {value}, target.token);
                } else if (target.kind == Expr::Kind::Member) {
                    emit_typed_effect(Op::StoreMember, target.inferred_type, target.value, {lvalue_base, value}, target.token);
                } else if (target.kind == Expr::Kind::Index) {
                    emit_typed_effect(Op::StoreIndex, target.inferred_type, "", {lvalue_base, lvalue_index, value}, target.token);
                } else if (target.kind == Expr::Kind::Unary && target.value == "*") {
                    emit_typed_effect(Op::StoreIndirect, target.inferred_type, "", {lvalue_pointer, value}, target.token);
                } else {
                    throw Error("internal compiler error: invalid assignment target reached HIR");
                }
                return;
            }
            case Stmt::Kind::Expression:
                (void)lower_expression(*statement.expression);
                return;
            case Stmt::Kind::Say: {
                const ValueId value = lower_expression(*statement.expression);
                emit_effect(Op::Say, "", {value}, statement.token);
                return;
            }
            case Stmt::Kind::Return: {
                const ValueId value = statement.expression ? lower_expression(*statement.expression) : NoValue;
                emit_drops_from(0);
                terminate_return(value, statement.token);
                return;
            }
            case Stmt::Kind::If: lower_if(statement); return;
            case Stmt::Kind::While: lower_while(statement); return;
            case Stmt::Kind::Each: lower_each(statement); return;
            case Stmt::Kind::Break:
                if (loops_.empty()) throw Error("internal compiler error: break escaped semantic validation");
                emit_drops_from(loops_.back().scope_base);
                terminate_jump(loops_.back().break_block, statement.token);
                return;
            case Stmt::Kind::Continue:
                if (loops_.empty()) throw Error("internal compiler error: continue escaped semantic validation");
                emit_drops_from(loops_.back().scope_base);
                terminate_jump(loops_.back().continue_block, statement.token);
                return;
            case Stmt::Kind::Unsafe:
                lower_block(statement.body);
                return;
        }
    }

    void lower_if(const Stmt &statement) {
        const ValueId condition = lower_expression(*statement.expression);
        const BlockId then_block = create_block();
        const BlockId else_block = statement.alternative.empty() ? NoBlock : create_block();
        const BlockId merge_block = create_block();
        terminate_branch(condition, then_block,
                         else_block == NoBlock ? merge_block : else_block, statement.token);

        current_ = then_block;
        lower_block(statement.body);
        const bool then_terminated = terminated();
        if (!then_terminated) terminate_jump(merge_block, statement.token);

        bool else_terminated = false;
        if (else_block != NoBlock) {
            current_ = else_block;
            lower_block(statement.alternative);
            else_terminated = terminated();
            if (!else_terminated) terminate_jump(merge_block, statement.token);
        }

        current_ = merge_block;
        if (else_block != NoBlock && then_terminated && else_terminated)
            terminate_unreachable(statement.token);
    }

    void lower_while(const Stmt &statement) {
        const BlockId condition_block = create_block();
        const BlockId body_block = create_block();
        const BlockId exit_block = create_block();
        terminate_jump(condition_block, statement.token);

        current_ = condition_block;
        const ValueId condition = lower_expression(*statement.expression);
        terminate_branch(condition, body_block, exit_block, statement.token);

        current_ = body_block;
        loops_.push_back({condition_block, exit_block, drop_scopes_.size()});
        lower_block(statement.body);
        loops_.pop_back();
        if (!terminated()) terminate_jump(condition_block, statement.token);
        current_ = exit_block;
    }

    void lower_each(const Stmt &statement) {
        const ValueId start = lower_expression(*statement.expression);
        const ValueId end = lower_expression(*statement.upper);
        const std::string counter = storage_name(statement.binding_id, statement.name);
        const std::string upper_name = temporary_storage("range.end");
        emit_effect(Op::Store, counter, {start}, statement.token);
        emit_effect(Op::Store, upper_name, {end}, statement.token);

        const BlockId condition_block = create_block();
        const BlockId body_block = create_block();
        const BlockId advance_block = create_block();
        const BlockId exit_block = create_block();
        terminate_jump(condition_block, statement.token);

        current_ = condition_block;
        const ValueId index = emit_value(Op::Load, Type::Int, counter, {}, statement.token);
        const ValueId upper = emit_value(Op::Load, Type::Int, upper_name, {}, statement.token);
        const ValueId condition = emit_value(Op::Binary, Type::Bool, "<", {index, upper}, statement.token);
        terminate_branch(condition, body_block, exit_block, statement.token);

        current_ = body_block;
        loops_.push_back({advance_block, exit_block, drop_scopes_.size()});
        lower_block(statement.body);
        loops_.pop_back();
        if (!terminated()) terminate_jump(advance_block, statement.token);

        current_ = advance_block;
        const ValueId old_index = emit_value(Op::Load, Type::Int, counter, {}, statement.token);
        const ValueId one = emit_value(Op::Constant, Type::Int, "1", {}, statement.token);
        const ValueId next = emit_value(Op::Binary, Type::Int, "+", {old_index, one}, statement.token);
        emit_effect(Op::Store, counter, {next}, statement.token);
        terminate_jump(condition_block, statement.token);

        current_ = exit_block;
    }

    ValueId lower_address(const Expr &target, Type result_type, const std::string &kind, const Token &token) {
        if (target.kind == Expr::Kind::Variable)
            return emit_value(Op::AddressOf, result_type, kind + " " + storage_name(target.binding_id, target.value), {}, token);
        if (target.kind == Expr::Kind::Member) {
            const Expr &parent = *target.children[0];
            ValueId base = NoValue;
            Type base_type = parent.inferred_type;
            const auto shape_it = shapes_.find(base_type.name);
            const bool object_identity = shape_it != shapes_.end() && shape_it->second->reference_type;
            if (object_identity || is_pointer_like_type(base_type)) {
                base = lower_expression(parent);
            } else {
                base = lower_address(parent, Type{"&mut " + base_type.name}, "&mut", parent.token);
            }
            return emit_value(Op::AddressOf, result_type, kind + " ." + target.value, {base}, token);
        }
        if (target.kind == Expr::Kind::Unary && target.value == "*")
            return lower_expression(*target.children[0]);
        throw Error("internal compiler error: non-addressable expression reached HIR address lowering");
    }

    ValueId lower_short_circuit(const Expr &expression) {
        const ValueId left = lower_expression(*expression.children[0]);
        const std::string result_storage = temporary_storage("logic");
        const BlockId right_block = create_block();
        const BlockId short_block = create_block();
        const BlockId merge_block = create_block();
        if (expression.value == "and") terminate_branch(left, right_block, short_block, expression.token);
        else terminate_branch(left, short_block, right_block, expression.token);

        current_ = short_block;
        const ValueId short_value = emit_value(Op::Constant, Type::Bool,
                                               expression.value == "and" ? "no" : "yes", {}, expression.token);
        emit_effect(Op::Store, result_storage, {short_value}, expression.token);
        terminate_jump(merge_block, expression.token);

        current_ = right_block;
        const ValueId right = lower_expression(*expression.children[1]);
        if (!terminated()) {
            emit_effect(Op::Store, result_storage, {right}, expression.token);
            terminate_jump(merge_block, expression.token);
        }

        current_ = merge_block;
        return emit_value(Op::Load, Type::Bool, result_storage, {}, expression.token);
    }

    void lower_pattern_tests(const Pattern &pattern, ValueId value, Type type,
                             BlockId success, BlockId failure,
                             std::vector<PendingPatternBinding> &bindings) {
        if (pattern.kind == Pattern::Kind::Wildcard) {
            terminate_jump(success, pattern.token);
            return;
        }
        if (pattern.kind == Pattern::Kind::Binding) {
            bindings.push_back({&pattern, value, type});
            terminate_jump(success, pattern.token);
            return;
        }
        if (pattern.kind == Pattern::Kind::Integer || pattern.kind == Pattern::Kind::String ||
            pattern.kind == Pattern::Kind::Boolean) {
            Type constant_type = pattern.kind == Pattern::Kind::Integer ? Type::Int :
                                 pattern.kind == Pattern::Kind::String ? Type::Str : Type::Bool;
            const ValueId constant = emit_value(Op::Constant, constant_type, pattern.value, {}, pattern.token);
            const ValueId condition = emit_value(Op::Binary, Type::Bool, "==", {value, constant}, pattern.token);
            terminate_branch(condition, success, failure, pattern.token);
            return;
        }

        const Shape &enum_shape = shape(type);
        const ValueId tag = emit_value(Op::Member, Type::Int, "__tag", {value}, pattern.token);
        const ValueId wanted = emit_value(Op::Constant, Type::Int, std::to_string(pattern.variant_index), {}, pattern.token);
        const ValueId tag_ok = emit_value(Op::Binary, Type::Bool, "==", {tag, wanted}, pattern.token);
        const BlockId payload_start = pattern.children.empty() ? success : create_block();
        terminate_branch(tag_ok, payload_start, failure, pattern.token);
        if (pattern.children.empty()) return;

        current_ = payload_start;
        const EnumVariant &variant = enum_shape.enum_variants.at(pattern.variant_index);
        for (std::size_t i = 0; i < pattern.children.size(); ++i) {
            const Type payload_type = variant.payload.at(i);
            const ValueId payload = emit_value(Op::Member, payload_type,
                                               "__v" + std::to_string(pattern.variant_index) + "_" + std::to_string(i),
                                               {value}, pattern.children[i].token);
            const BlockId child_success = i + 1 == pattern.children.size() ? success : create_block();
            lower_pattern_tests(pattern.children[i], payload, payload_type, child_success, failure, bindings);
            if (i + 1 != pattern.children.size()) current_ = child_success;
        }
    }

    ValueId lower_match(const Expr &expression) {
        const ValueId subject = lower_expression(*expression.children[0]);
        const Type subject_type = expression.children[0]->inferred_type;
        const Type result_type = expression.inferred_type;
        const std::string result_storage = result_type == Type::Void ? std::string{} : temporary_storage("match.result");
        const BlockId merge_block = create_block();
        BlockId test_block = current_;

        for (std::size_t i = 0; i < expression.match_patterns.size(); ++i) {
            current_ = test_block;
            const BlockId arm_block = create_block();
            const BlockId next_test = create_block();
            std::vector<PendingPatternBinding> bindings;
            lower_pattern_tests(expression.match_patterns[i], subject, subject_type, arm_block, next_test, bindings);

            current_ = arm_block;
            drop_scopes_.push_back({});
            for (const PendingPatternBinding &binding : bindings) {
                const std::string storage = storage_name(binding.pattern->binding_id, binding.pattern->value);
                emit_effect(Op::Store, storage, {binding.value}, binding.pattern->token);
                register_drop(storage, binding.type, binding.pattern->token);
            }
            const ValueId arm_value = lower_expression(*expression.children[i + 1]);
            if (!terminated() && result_type != Type::Void)
                emit_effect(Op::Store, result_storage, {arm_value}, expression.children[i + 1]->token);
            if (!terminated()) emit_scope_drops(drop_scopes_.size() - 1);
            drop_scopes_.pop_back();
            if (!terminated()) terminate_jump(merge_block, expression.children[i + 1]->token);
            test_block = next_test;
        }

        current_ = test_block;
        terminate_unreachable(expression.token); // semantic analysis proved exhaustiveness
        current_ = merge_block;
        if (result_type == Type::Void) return NoValue;
        return emit_value(Op::Load, result_type, result_storage, {}, expression.token);
    }

    ValueId lower_propagate(const Expr &expression) {
        if (!source_function_)
            throw Error("internal compiler error: propagation lowered outside a function");
        const ValueId source = lower_expression(*expression.children[0]);
        const Type source_type = expression.children[0]->inferred_type;
        const Shape &source_shape = shape(source_type);
        const std::size_t success_index = variant_index(source_shape, expression.enum_variant);
        const std::string failure_name = expression.enum_variant == "Some" ? "None" : "Error";
        const std::size_t source_failure = variant_index(source_shape, failure_name);

        const ValueId tag = emit_value(Op::Member, Type::Int, "__tag", {source}, expression.token);
        const ValueId wanted = emit_value(Op::Constant, Type::Int, std::to_string(success_index), {}, expression.token);
        const ValueId ok = emit_value(Op::Binary, Type::Bool, "==", {tag, wanted}, expression.token);
        const BlockId success_block = create_block();
        const BlockId failure_block = create_block();
        const BlockId merge_block = create_block();
        terminate_branch(ok, success_block, failure_block, expression.token);

        current_ = failure_block;
        std::vector<ValueId> failure_payload;
        if (!source_shape.enum_variants.at(source_failure).payload.empty()) {
            const Type payload_type = source_shape.enum_variants.at(source_failure).payload.at(0);
            failure_payload.push_back(emit_value(Op::Member, payload_type,
                                                 "__v" + std::to_string(source_failure) + "_0",
                                                 {source}, expression.token));
        }
        const ValueId failure_value = emit_value(Op::EnumConstruct, source_function_->result,
                                                 source_function_->result.name + "::" + failure_name,
                                                 std::move(failure_payload), expression.token);
        emit_drops_from(0);
        terminate_return(failure_value, expression.token);

        current_ = success_block;
        const Type payload_type = source_shape.enum_variants.at(success_index).payload.at(0);
        const ValueId payload = emit_value(Op::Member, payload_type,
                                           "__v" + std::to_string(success_index) + "_0",
                                           {source}, expression.token);
        const std::string result_storage = temporary_storage("propagate.result");
        emit_effect(Op::Store, result_storage, {payload}, expression.token);
        terminate_jump(merge_block, expression.token);

        current_ = merge_block;
        return emit_value(Op::Load, expression.inferred_type, result_storage, {}, expression.token);
    }

    ValueId lower_expression(const Expr &expression) {
        const Type type = expression.inferred_type;
        if (type == Type::Infer)
            throw Error("internal compiler error: untyped expression reached HIR lowering at " + where(expression.token));

        switch (expression.kind) {
            case Expr::Kind::Integer:
            case Expr::Kind::Float:
            case Expr::Kind::String:
            case Expr::Kind::Boolean:
                return emit_value(Op::Constant, type, expression.value, {}, expression.token);
            case Expr::Kind::Variable:
                return emit_value(expression.consumes_value ? Op::MoveLoad : Op::Load, type,
                                  storage_name(expression.binding_id, expression.value), {}, expression.token);
            case Expr::Kind::Call:
            case Expr::Kind::MethodCall: {
                std::vector<ValueId> arguments;
                for (const auto &child : expression.children) arguments.push_back(lower_expression(*child));
                const Op op = expression.kind == Expr::Kind::Call && shape_names_.count(expression.value) ? Op::Construct : Op::Call;
                if (type == Type::Void) {
                    emit_effect(op, expression.value, std::move(arguments), expression.token);
                    return NoValue;
                }
                return emit_value(op, type, expression.value, std::move(arguments), expression.token);
            }
            case Expr::Kind::EnumConstruct: {
                std::vector<ValueId> arguments;
                for (const auto &child : expression.children) arguments.push_back(lower_expression(*child));
                return emit_value(Op::EnumConstruct, type, expression.value + "::" + expression.enum_variant,
                                  std::move(arguments), expression.token);
            }
            case Expr::Kind::Match:
                return lower_match(expression);
            case Expr::Kind::Propagate:
                return lower_propagate(expression);
            case Expr::Kind::SizeOf:
                return emit_value(Op::SizeOf, type, expression.value, {}, expression.token);
            case Expr::Kind::AlignOf:
                return emit_value(Op::AlignOf, type, expression.value, {}, expression.token);
            case Expr::Kind::Unary: {
                if (expression.value == "&" || expression.value == "&mut" || expression.value == "&raw")
                    return lower_address(*expression.children[0], type, expression.value, expression.token);
                const ValueId value = lower_expression(*expression.children[0]);
                if (expression.value == "await") {
                    if (type == Type::Void) {
                        emit_effect(Op::Await, "", {value}, expression.token);
                        return NoValue;
                    }
                    return emit_value(Op::Await, type, "", {value}, expression.token);
                }
                if (expression.value == "*") return emit_value(Op::Deref, type, "", {value}, expression.token);
                return emit_value(Op::Unary, type, expression.value, {value}, expression.token);
            }
            case Expr::Kind::Binary:
                if (expression.value == "and" || expression.value == "or") return lower_short_circuit(expression);
                else {
                    const ValueId left = lower_expression(*expression.children[0]);
                    const ValueId right = lower_expression(*expression.children[1]);
                    return emit_value(Op::Binary, type, expression.value, {left, right}, expression.token);
                }
            case Expr::Kind::Member: {
                const ValueId base = lower_expression(*expression.children[0]);
                return emit_value(Op::Member, type, expression.value, {base}, expression.token);
            }
            case Expr::Kind::Index: {
                const ValueId base = lower_expression(*expression.children[0]);
                const ValueId index = lower_expression(*expression.children[1]);
                return emit_value(Op::Index, type, "", {base, index}, expression.token);
            }
            case Expr::Kind::List: {
                std::vector<ValueId> values;
                for (const auto &child : expression.children) values.push_back(lower_expression(*child));
                return emit_value(Op::List, type, "", std::move(values), expression.token);
            }
        }
        throw Error("internal compiler error: unknown expression reached HIR lowering");
    }
};


inline std::string dump(const Program &program) {
    std::ostringstream out;
    for (const Function &function : program.functions) {
        out << "func @" << function.name << "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << function.parameters[i].first << ": " << type_name(function.parameters[i].second);
        }
        out << ") -> " << type_name(function.result);
        if (function.is_async) out << " async";
        if (function.external_native) {
            out << " extern-native @" << function.native_symbol << "\n";
            continue;
        }
        out << " {\n";
        for (const Block &block : function.blocks) {
            out << "  bb" << block.id << ":\n";
            for (const Instruction &instruction : block.instructions) {
                out << "    ";
                if (instruction.result != NoValue)
                    out << "%" << instruction.result << ":" << type_name(instruction.type) << " = ";
                out << op_name(instruction.op);
                if (!instruction.detail.empty()) out << " " << instruction.detail;
                if (!instruction.operands.empty()) {
                    out << " ";
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        if (i) out << ", ";
                        out << "%" << instruction.operands[i];
                    }
                }
                out << "\n";
            }
            const Terminator &term = block.terminator;
            out << "    ";
            switch (term.kind) {
                case Terminator::Kind::Jump: out << "jump bb" << term.first; break;
                case Terminator::Kind::Branch:
                    out << "branch %" << term.value << ", bb" << term.first << ", bb" << term.second; break;
                case Terminator::Kind::Return:
                    out << "return";
                    if (term.value != NoValue) out << " %" << term.value;
                    break;
                case Terminator::Kind::Unreachable: out << "unreachable"; break;
                case Terminator::Kind::None: out << "<unterminated>"; break;
            }
            out << "\n";
        }
        out << "}\n";
    }
    return out.str();
}

} // namespace pphir

#endif
