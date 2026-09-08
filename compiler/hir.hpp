#ifndef PUNPUN_HIR_HPP
#define PUNPUN_HIR_HPP

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "frontend.hpp"

namespace pphir {

using ValueId = std::size_t;
using BlockId = std::size_t;
constexpr ValueId NoValue = static_cast<ValueId>(-1);
constexpr BlockId NoBlock = static_cast<BlockId>(-1);

enum class Op {
    Parameter, Constant, Load, Store, Call, Construct, EnumConstruct, Match, Propagate, Await, Unary, Binary, Member, Index, List,
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
        case Op::Store: return "store";
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
            for (const Shape &shape : module.shapes) shapes_.insert(shape.name);
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

            std::vector<bool> values(function.value_count, false);
            for (std::size_t expected = 0; expected < function.blocks.size(); ++expected) {
                const Block &block = function.blocks[expected];
                if (block.id != expected)
                    throw Error("internal compiler error: non-canonical HIR block numbering");
                for (const Instruction &instruction : block.instructions) {
                    for (ValueId operand : instruction.operands)
                        if (operand == NoValue || operand >= values.size() || !values[operand])
                            throw Error("internal compiler error: HIR instruction uses undefined value");
                    if (instruction.result != NoValue) {
                        if (instruction.result >= values.size() || values[instruction.result])
                            throw Error("internal compiler error: duplicate/out-of-range HIR value");
                        if (instruction.type == Type::Void)
                            throw Error("internal compiler error: void HIR instruction defines a value");
                        values[instruction.result] = true;
                    }
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
    const std::vector<Module> &modules_;
    std::unordered_set<std::string> shapes_;
    Function *function_ = nullptr;
    BlockId current_ = 0;
    ValueId next_value_ = 0;
    std::vector<std::pair<BlockId, BlockId>> loops_; // continue, break

    static void check_block(BlockId block, std::size_t count) {
        if (block == NoBlock || block >= count)
            throw Error("internal compiler error: HIR terminator targets invalid block");
    }
    static void check_value(ValueId value, const std::vector<bool> &defined) {
        if (value == NoValue || value >= defined.size() || !defined[value])
            throw Error("internal compiler error: HIR terminator uses undefined value");
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
        current_ = create_block();
        next_value_ = 0;
        loops_.clear();

        for (const Parameter &parameter : source.parameters) {
            function.parameters.push_back({parameter.name, parameter.type});
            const ValueId value = emit_value(Op::Parameter, parameter.type, parameter.name, {}, parameter.token);
            emit_effect(Op::Store, parameter.name, {value}, parameter.token);
        }

        lower_statements(source.body);
        if (!terminated()) {
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
                emit_effect(Op::Store, statement.name, {value}, statement.token);
                return;
            }
            case Stmt::Kind::Assign: {
                const Expr &target = *statement.target;
                ValueId value = lower_expression(*statement.expression);
                if (statement.assignment_op != "=" && statement.assignment_op != "<-") {
                    const ValueId current = lower_expression(target);
                    const std::string op(1, statement.assignment_op.front());
                    value = emit_value(Op::Binary, target.inferred_type, op, {current, value}, statement.token);
                }
                if (target.kind == Expr::Kind::Variable) {
                    emit_effect(Op::Store, target.value, {value}, target.token);
                } else if (target.kind == Expr::Kind::Member) {
                    const ValueId base = lower_expression(*target.children[0]);
                    emit_effect(Op::StoreMember, target.value, {base, value}, target.token);
                } else if (target.kind == Expr::Kind::Index) {
                    const ValueId base = lower_expression(*target.children[0]);
                    const ValueId index = lower_expression(*target.children[1]);
                    emit_effect(Op::StoreIndex, "", {base, index, value}, target.token);
                } else if (target.kind == Expr::Kind::Unary && target.value == "*") {
                    const ValueId pointer = lower_expression(*target.children[0]);
                    emit_effect(Op::StoreIndirect, "", {pointer, value}, target.token);
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
                terminate_return(value, statement.token);
                return;
            }
            case Stmt::Kind::If: lower_if(statement); return;
            case Stmt::Kind::While: lower_while(statement); return;
            case Stmt::Kind::Each: lower_each(statement); return;
            case Stmt::Kind::Break:
                if (loops_.empty()) throw Error("internal compiler error: break escaped semantic validation");
                terminate_jump(loops_.back().second, statement.token);
                return;
            case Stmt::Kind::Continue:
                if (loops_.empty()) throw Error("internal compiler error: continue escaped semantic validation");
                terminate_jump(loops_.back().first, statement.token);
                return;
            case Stmt::Kind::Unsafe:
                lower_statements(statement.body);
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
        lower_statements(statement.body);
        const bool then_terminated = terminated();
        if (!then_terminated) terminate_jump(merge_block, statement.token);

        bool else_terminated = false;
        if (else_block != NoBlock) {
            current_ = else_block;
            lower_statements(statement.alternative);
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
        loops_.push_back({condition_block, exit_block});
        lower_statements(statement.body);
        loops_.pop_back();
        if (!terminated()) terminate_jump(condition_block, statement.token);
        current_ = exit_block;
    }

    void lower_each(const Stmt &statement) {
        const ValueId start = lower_expression(*statement.expression);
        const ValueId end = lower_expression(*statement.upper);
        const std::string upper_name = "$range.end." + std::to_string(current_) + "." + statement.name;
        emit_effect(Op::Store, statement.name, {start}, statement.token);
        emit_effect(Op::Store, upper_name, {end}, statement.token);

        const BlockId condition_block = create_block();
        const BlockId body_block = create_block();
        const BlockId advance_block = create_block();
        const BlockId exit_block = create_block();
        terminate_jump(condition_block, statement.token);

        current_ = condition_block;
        const ValueId index = emit_value(Op::Load, Type::Int, statement.name, {}, statement.token);
        const ValueId upper = emit_value(Op::Load, Type::Int, upper_name, {}, statement.token);
        const ValueId condition = emit_value(Op::Binary, Type::Bool, "<", {index, upper}, statement.token);
        terminate_branch(condition, body_block, exit_block, statement.token);

        current_ = body_block;
        loops_.push_back({advance_block, exit_block});
        lower_statements(statement.body);
        loops_.pop_back();
        if (!terminated()) terminate_jump(advance_block, statement.token);

        current_ = advance_block;
        const ValueId old_index = emit_value(Op::Load, Type::Int, statement.name, {}, statement.token);
        const ValueId one = emit_value(Op::Constant, Type::Int, "1", {}, statement.token);
        const ValueId next = emit_value(Op::Binary, Type::Int, "+", {old_index, one}, statement.token);
        emit_effect(Op::Store, statement.name, {next}, statement.token);
        terminate_jump(condition_block, statement.token);

        current_ = exit_block;
    }

    ValueId lower_address(const Expr &target, Type result_type, const std::string &kind, const Token &token) {
        if (target.kind == Expr::Kind::Variable)
            return emit_value(Op::AddressOf, result_type, kind + " " + target.value, {}, token);
        if (target.kind == Expr::Kind::Member) {
            const ValueId base = lower_expression(*target.children[0]);
            return emit_value(Op::AddressOf, result_type, kind + " ." + target.value, {base}, token);
        }
        if (target.kind == Expr::Kind::Unary && target.value == "*")
            return lower_expression(*target.children[0]);
        throw Error("internal compiler error: non-addressable expression reached HIR address lowering");
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
                return emit_value(Op::Load, type, expression.value, {}, expression.token);
            case Expr::Kind::Call:
            case Expr::Kind::MethodCall: {
                std::vector<ValueId> arguments;
                for (const auto &child : expression.children) arguments.push_back(lower_expression(*child));
                const Op op = expression.kind == Expr::Kind::Call && shapes_.count(expression.value) ? Op::Construct : Op::Call;
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
            case Expr::Kind::Match: {
                std::vector<ValueId> values;
                for (const auto &child : expression.children) values.push_back(lower_expression(*child));
                if (type == Type::Void) {
                    emit_effect(Op::Match, std::to_string(expression.match_patterns.size()), std::move(values), expression.token);
                    return NoValue;
                }
                return emit_value(Op::Match, type, std::to_string(expression.match_patterns.size()), std::move(values), expression.token);
            }
            case Expr::Kind::Propagate: {
                const ValueId value = lower_expression(*expression.children[0]);
                return emit_value(Op::Propagate, type, expression.value, {value}, expression.token);
            }
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
            case Expr::Kind::Binary: {
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
