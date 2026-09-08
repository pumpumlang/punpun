#ifndef PUNPUN_DEBUG_DUMP_HPP
#define PUNPUN_DEBUG_DUMP_HPP

#include <sstream>
#include <string>
#include <vector>

#include "frontend.hpp"

namespace ppdebug {

inline const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Word: return "word";
        case TokenKind::Integer: return "integer";
        case TokenKind::Float: return "float";
        case TokenKind::String: return "string";
        case TokenKind::Symbol: return "symbol";
        case TokenKind::End: return "end";
    }
    return "unknown";
}

inline std::string escaped(const std::string &value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            default:
                if (c >= 32 && c < 127) out += static_cast<char>(c);
                else {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\x";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                }
        }
    }
    return out;
}

inline std::string dump_tokens(const std::vector<Token> &tokens) {
    std::ostringstream out;
    for (const Token &token : tokens) {
        out << token.file.string() << ':' << token.line << ':' << token.column
            << "  " << token_kind_name(token.kind)
            << "  [" << token.offset << ".." << (token.offset + token.length) << ")";
        if (token.kind != TokenKind::End) out << "  \"" << escaped(token.text) << '"';
        out << '\n';
    }
    return out.str();
}

inline const char *expr_kind_name(Expr::Kind kind) {
    switch (kind) {
        case Expr::Kind::Integer: return "Integer";
        case Expr::Kind::Float: return "Float";
        case Expr::Kind::String: return "String";
        case Expr::Kind::Boolean: return "Boolean";
        case Expr::Kind::Variable: return "Variable";
        case Expr::Kind::Call: return "Call";
        case Expr::Kind::MethodCall: return "MethodCall";
        case Expr::Kind::Unary: return "Unary";
        case Expr::Kind::Binary: return "Binary";
        case Expr::Kind::Member: return "Member";
        case Expr::Kind::Index: return "Index";
        case Expr::Kind::List: return "List";
        case Expr::Kind::SizeOf: return "SizeOf";
        case Expr::Kind::AlignOf: return "AlignOf";
    }
    return "UnknownExpr";
}

inline const char *stmt_kind_name(Stmt::Kind kind) {
    switch (kind) {
        case Stmt::Kind::Variable: return "Variable";
        case Stmt::Kind::Assign: return "Assign";
        case Stmt::Kind::Expression: return "Expression";
        case Stmt::Kind::Return: return "Return";
        case Stmt::Kind::If: return "If";
        case Stmt::Kind::While: return "While";
        case Stmt::Kind::Each: return "Each";
        case Stmt::Kind::Break: return "Break";
        case Stmt::Kind::Continue: return "Continue";
        case Stmt::Kind::Say: return "Say";
        case Stmt::Kind::Unsafe: return "Unsafe";
    }
    return "UnknownStmt";
}

inline void indent(std::ostringstream &out, int depth) {
    out << std::string(static_cast<size_t>(depth) * 2, ' ');
}

inline void dump_expr(std::ostringstream &out, const Expr &expr, int depth) {
    indent(out, depth);
    out << expr_kind_name(expr.kind);
    if (!expr.value.empty()) out << " value=\"" << escaped(expr.value) << '"';
    out << " @" << expr.token.line << ':' << expr.token.column
        << " [" << expr.token.offset << ".." << (expr.token.offset + expr.token.length) << ")\n";
    for (const auto &child : expr.children) dump_expr(out, *child, depth + 1);
}

inline void dump_stmt(std::ostringstream &out, const Stmt &stmt, int depth) {
    indent(out, depth);
    out << stmt_kind_name(stmt.kind);
    if (!stmt.name.empty()) out << " name=" << stmt.name;
    if (stmt.declared_type != Type::Infer) out << " type=" << type_name(stmt.declared_type);
    if (stmt.kind == Stmt::Kind::Variable) out << (stmt.mutable_value ? " mutable" : " immutable");
    out << " @" << stmt.token.line << ':' << stmt.token.column << '\n';
    if (stmt.target) {
        indent(out, depth + 1); out << "target:\n";
        dump_expr(out, *stmt.target, depth + 2);
    }
    if (stmt.expression) {
        indent(out, depth + 1); out << "expression:\n";
        dump_expr(out, *stmt.expression, depth + 2);
    }
    if (stmt.upper) {
        indent(out, depth + 1); out << "upper:\n";
        dump_expr(out, *stmt.upper, depth + 2);
    }
    if (!stmt.body.empty()) {
        indent(out, depth + 1); out << "body:\n";
        for (const auto &child : stmt.body) dump_stmt(out, child, depth + 2);
    }
    if (!stmt.alternative.empty()) {
        indent(out, depth + 1); out << "alternative:\n";
        for (const auto &child : stmt.alternative) dump_stmt(out, child, depth + 2);
    }
}

inline std::string dump_modules(const std::vector<Module> &modules) {
    std::ostringstream out;
    for (const Module &module : modules) {
        out << "Module \"" << module.file.string() << "\"\n";
        for (const std::string &name : module.imports) out << "  Import " << name << '\n';
        for (const Shape &shape : module.shapes) {
            out << "  Shape " << shape.name;
            if (!shape.generic_parameters.empty()) {
                out << '<';
                for (size_t i = 0; i < shape.generic_parameters.size(); ++i) {
                    if (i) out << ", ";
                    out << shape.generic_parameters[i].name;
                    if (!shape.generic_parameters[i].constraints.empty()) {
                        out << ": ";
                        for (size_t c = 0; c < shape.generic_parameters[i].constraints.size(); ++c) {
                            if (c) out << " + ";
                            out << type_name(shape.generic_parameters[i].constraints[c]);
                        }
                    }
                }
                out << '>';
            }
            out << " @" << shape.token.line << ':' << shape.token.column << '\n';
            for (const Parameter &field : shape.fields)
                out << "    Field " << field.name << ": " << type_name(field.type)
                    << " @" << field.token.line << ':' << field.token.column << '\n';
        }
        for (const Function &function : module.functions) {
            out << "  Function " << function.name;
            if (!function.generic_parameters.empty()) {
                out << '<';
                for (size_t i = 0; i < function.generic_parameters.size(); ++i) {
                    if (i) out << ", ";
                    out << function.generic_parameters[i].name;
                    if (!function.generic_parameters[i].constraints.empty()) {
                        out << ": ";
                        for (size_t c = 0; c < function.generic_parameters[i].constraints.size(); ++c) {
                            if (c) out << " + ";
                            out << type_name(function.generic_parameters[i].constraints[c]);
                        }
                    }
                }
                out << '>';
            }
            out << '(';
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                if (i) out << ", ";
                out << function.parameters[i].name << ": " << type_name(function.parameters[i].type);
            }
            out << ") -> " << type_name(function.result)
                << " @" << function.token.line << ':' << function.token.column << '\n';
            for (const Stmt &statement : function.body) dump_stmt(out, statement, 2);
        }
    }
    return out.str();
}

}  // namespace ppdebug

#endif  // PUNPUN_DEBUG_DUMP_HPP
