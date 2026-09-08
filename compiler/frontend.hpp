// PunPun compiler frontend: source spans, lexer, AST, and parser.
//
// 0.5 keeps the 0.4 grammar as a migration dialect while making the modern
// brace-based grammar authoritative.  Both dialects lower into the SAME AST;
// there is deliberately no second parser/compiler pipeline.
#ifndef PUNPUN_FRONTEND_HPP
#define PUNPUN_FRONTEND_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "diagnostics.hpp"

namespace fs = std::filesystem;

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

enum class TokenKind { Word, Integer, Float, String, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
    fs::path file;
    int line;
    int column;
    size_t offset = 0;
    size_t length = 1;
};

inline std::string where(const Token &token) {
    return token.file.string() + ":" + std::to_string(token.line) + ":" + std::to_string(token.column);
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
class Lexer {
  public:
    Lexer(fs::path file, std::string source) : file_(std::move(file)), source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> result;
        while (!at_end()) {
            skip_space_and_comments();
            if (at_end()) break;
            const int start_line = line_;
            const int start_column = column_;
            const size_t start_offset = index_;
            const char c = advance();

            if (c == '\n') {
                // Newlines remain statement separators even inside braces. They
                // are suppressed only inside parens/brackets where expressions
                // commonly span lines.
                if (expression_nesting_ == 0)
                    result.push_back({TokenKind::Symbol, ";", file_, start_line, start_column,
                                      start_offset, index_ - start_offset});
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string text(1, c);
                while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
                    text += advance();
                result.push_back({TokenKind::Word, text, file_, start_line, start_column,
                                  start_offset, index_ - start_offset});
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::string text(1, c);
                while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) text += advance();
                TokenKind kind = TokenKind::Integer;
                if (!at_end() && peek() == '.' && index_ + 1 < source_.size() &&
                    source_[index_ + 1] != '.' && std::isdigit(static_cast<unsigned char>(source_[index_ + 1]))) {
                    kind = TokenKind::Float;
                    text += advance();
                    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) text += advance();
                }
                result.push_back({kind, text, file_, start_line, start_column, start_offset, index_ - start_offset});
                continue;
            }
            if (c == '"') {
                std::string value;
                // Triple-quoted literals preserve newlines verbatim. They are
                // primarily useful for explicit @inject blocks, but remain a
                // normal String token so tooling needs no second lexer.
                if (index_ + 1 < source_.size() && peek() == '"' && source_[index_ + 1] == '"') {
                    advance(); advance();
                    while (!at_end()) {
                        if (peek() == '"' && index_ + 2 < source_.size() && source_[index_ + 1] == '"' && source_[index_ + 2] == '"') {
                            advance(); advance(); advance();
                            break;
                        }
                        value += advance();
                    }
                    if (index_ < 3 || source_.substr(index_ - 3, 3) != "\"\"\"")
                        fail(start_line, start_column, "unterminated triple-quoted string");
                    result.push_back({TokenKind::String, value, file_, start_line, start_column, start_offset, index_ - start_offset});
                    continue;
                }
                while (!at_end() && peek() != '"') {
                    char next = advance();
                    if (next == '\\') {
                        if (at_end()) fail(start_line, start_column, "unterminated string");
                        const char escaped = advance();
                        if (escaped == 'n') value += '\n';
                        else if (escaped == 't') value += '\t';
                        else if (escaped == 'r') value += '\r';
                        else if (escaped == '"') value += '"';
                        else if (escaped == '\\') value += '\\';
                        else fail(line_, column_ - 1, "unknown string escape");
                    } else {
                        if (next == '\n' || next == '\0') fail(start_line, start_column, "invalid string character");
                        value += next;
                    }
                }
                if (at_end()) fail(start_line, start_column, "unterminated string");
                advance();
                result.push_back({TokenKind::String, value, file_, start_line, start_column,
                                  start_offset, index_ - start_offset});
                continue;
            }

            std::string symbol(1, c);
            if (!at_end()) {
                const std::string pair = symbol + peek();
                static const std::unordered_set<std::string> pairs = {
                    "<-", "->", "==", "!=", "<=", ">=", "&&", "||", "..", "::",
                    "+=", "-=", "*=", "/=", "<<", ">>", "=>"};
                if (pairs.count(pair)) symbol += advance();
            }
            static const std::unordered_set<std::string> valid = {
                "(", ")", "[", "]", "{", "}", ":", ";", ",", ".", "+", "-", "*", "/",
                "%", "=", "==", "!=", "<", "<=", ">", ">=", "<-", "->", "&&", "||", "!",
                "&", "|", "^", "~", "<<", ">>", "..", "::", "+=", "-=", "*=", "/=", "@", "=>", "?"};
            if (!valid.count(symbol)) fail(start_line, start_column, "unexpected character '" + symbol + "'");
            if (symbol == "(" || symbol == "[") ++expression_nesting_;
            if (symbol == ")" || symbol == "]") {
                if (expression_nesting_ == 0) fail(start_line, start_column, "unmatched closing delimiter");
                --expression_nesting_;
            }
            result.push_back({TokenKind::Symbol, symbol, file_, start_line, start_column,
                              start_offset, index_ - start_offset});
        }
        result.push_back({TokenKind::End, "", file_, line_, column_, source_.size(), 0});
        return result;
    }

  private:
    fs::path file_;
    std::string source_;
    size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;
    int expression_nesting_ = 0;

    bool at_end() const { return index_ >= source_.size(); }
    char peek() const { return source_[index_]; }
    char advance() {
        const char c = source_[index_++];
        if (c == '\n') { ++line_; column_ = 1; }
        else ++column_;
        return c;
    }
    void skip_space_and_comments() {
        for (;;) {
            while (!at_end() && peek() != '\n' && std::isspace(static_cast<unsigned char>(peek()))) advance();
            if (!at_end() && (peek() == '#' ||
                (peek() == '/' && index_ + 1 < source_.size() && source_[index_ + 1] == '/'))) {
                while (!at_end() && peek() != '\n') advance();
            } else break;
        }
    }
    [[noreturn]] void fail(int line, int column, const std::string &message) const {
        throw Error(ppdiag::format(file_, line, column, 1, "E0001", message, message));
    }
};

// ---------------------------------------------------------------------------
// Types and AST
// ---------------------------------------------------------------------------
struct Type {
    std::string name;
    bool operator==(const Type &other) const { return name == other.name; }
    bool operator!=(const Type &other) const { return !(*this == other); }
    static const Type Infer, Void, Int, Float, Bool, Str, Nums;
};

struct Pattern {
    enum class Kind { Wildcard, Binding, Variant, Integer, String, Boolean } kind = Kind::Wildcard;
    Token token;
    std::string value;
    std::vector<Pattern> children;
    std::size_t variant_index = 0;
    std::vector<Type> payload_types;
};
inline const Type Type::Infer{"inferred"};
inline const Type Type::Void{"void"};
inline const Type Type::Int{"int"};
inline const Type Type::Float{"float"};
inline const Type Type::Bool{"bool"};
inline const Type Type::Str{"str"};
inline const Type Type::Nums{"nums"};

inline std::string type_name(Type type) { return type.name; }
inline bool is_reference_type(const Type &type) { return type.name.rfind("&", 0) == 0; }
inline bool is_raw_pointer_type(const Type &type) { return type.name.rfind("*", 0) == 0; }
inline bool is_pointer_like_type(const Type &type) { return is_reference_type(type) || is_raw_pointer_type(type); }
inline bool is_mut_reference_type(const Type &type) { return type.name.rfind("&mut ", 0) == 0; }
inline bool is_task_type(const Type &type) { return type.name.rfind("@task:", 0) == 0; }
inline Type task_type(Type result) { return Type{"@task:" + result.name}; }
inline Type task_result_type(const Type &type) { return is_task_type(type) ? Type{type.name.substr(6)} : Type::Infer; }
inline Type pointee_type(const Type &type) {
    if (type.name.rfind("&mut ", 0) == 0) return Type{type.name.substr(5)};
    if (type.name.rfind("&", 0) == 0 || type.name.rfind("*", 0) == 0) return Type{type.name.substr(1)};
    return Type::Infer;
}

struct Expr {
    enum class Kind {
        Integer, Float, String, Boolean, Variable, Call, MethodCall, Unary, Binary,
        Member, Index, List, SizeOf, AlignOf, EnumConstruct, Match, Propagate
    } kind;
    Token token;
    std::string value;
    std::vector<std::unique_ptr<Expr>> children;
    // Empty entries are positional. Call expressions keep this parallel to
    // children so semantic analysis can reorder named arguments once the
    // selected signature is known.
    std::vector<std::string> argument_names;
    std::vector<Type> type_arguments;
    std::vector<Pattern> match_patterns;
    std::string enum_variant;
    Type inferred_type = Type::Infer;
};

enum class Visibility { Public, Private, Protected };

struct Stmt {
    enum class Kind {
        Variable, Assign, Expression, Return, If, While, Each, Break, Continue, Say, Unsafe
    } kind;
    Token token;
    std::string name;
    Type declared_type = Type::Infer;
    bool mutable_value = false;
    bool constant_value = false;
    std::string assignment_op = "=";
    std::unique_ptr<Expr> expression;
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> upper;
    std::vector<Stmt> body;
    std::vector<Stmt> alternative;

    Stmt(Kind kind, Token token) : kind(kind), token(std::move(token)) {}
};

struct Parameter {
    Token token;
    std::string name;
    Type type;
    bool mutable_value = false;
    Visibility visibility = Visibility::Public;  // fields use this; parameters ignore it
    std::shared_ptr<Expr> default_value;
};

struct GenericParameter {
    Token token;
    std::string name;
    std::vector<Type> constraints;
};

struct Function {
    Token token;
    std::string name;          // globally unique lowered name, e.g. Player::hit
    std::string source_name;   // source spelling, e.g. hit
    std::string owner_type;    // empty for free functions
    std::vector<GenericParameter> generic_parameters;
    std::vector<Parameter> parameters;
    Type result = Type::Void;
    std::vector<Stmt> body;
    bool is_method = false;
    bool is_initializer = false;
    bool self_mutable = false;
    Visibility visibility = Visibility::Public;
    bool is_extern_native = false;
    bool is_async = false;
    std::string native_symbol;
};

struct ForeignInjection {
    Token token;
    Token source_token;
    std::string language;
    std::string source;
};

struct ContractMethod {
    Token token;
    std::string name;
    std::vector<Parameter> parameters;
    Type result = Type::Void;
};

struct Contract {
    Token token;
    std::string name;
    std::vector<GenericParameter> generic_parameters;
    std::vector<ContractMethod> methods;
};

struct EnumVariant {
    Token token;
    std::string name;
    std::vector<Type> payload;
};

struct EnumDecl {
    Token token;
    std::string name;
    std::vector<GenericParameter> generic_parameters;
    std::vector<EnumVariant> variants;
};

struct Shape {
    Token token;
    std::string name;
    std::vector<GenericParameter> generic_parameters;
    std::vector<Parameter> fields;
    bool reference_type = false;     // struct=false, object=true
    bool sealed_type = false;
    std::string initializer_name;    // lowered function name or empty
    std::vector<std::string> method_names;
    std::vector<std::string> contracts; // zero-cost compile-time conformance metadata
    bool enum_type = false;
    std::vector<EnumVariant> enum_variants;
};

struct Module {
    fs::path file;
    std::vector<std::string> imports;
    std::vector<Function> functions;
    std::vector<Shape> shapes;
    std::vector<Contract> contracts;
    std::vector<EnumDecl> enums;
    std::vector<ForeignInjection> injections;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class Parser {
  public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Module parse(const fs::path &file) {
        Module module;
        module.file = file;
        separators();
        while (!is_end()) {
            if (check("@")) module.injections.push_back(parse_injection());
            else if (check("extern")) module.functions.push_back(parse_extern_native());
            else if (match("import")) module.imports.push_back(parse_modern_import());
            else if (match("bring")) module.imports.push_back(parse_legacy_import());
            else if (check("contract")) module.contracts.push_back(parse_contract());
            else if (check("struct") || check("object") || check("sealed")) parse_modern_shape(module);
            else if (check("enum")) module.enums.push_back(parse_enum());
            else if (match("shape")) module.shapes.push_back(parse_legacy_shape());
            else if (match("async")) {
                if (!check("fn")) fail(peek(), "expected 'fn' after 'async'");
                module.functions.push_back(parse_modern_function("", false, Visibility::Public, false, true));
            }
            else if (check("fn")) module.functions.push_back(parse_modern_function("", false, Visibility::Public, false));
            else module.functions.push_back(parse_legacy_function());
            separators();
        }
        return module;
    }

  private:
    std::vector<Token> tokens_;
    size_t current_ = 0;

    const Token &peek(size_t offset = 0) const { return tokens_[std::min(current_ + offset, tokens_.size() - 1)]; }
    bool is_end() const { return peek().kind == TokenKind::End; }
    bool check(const std::string &text) const {
        return (peek().kind == TokenKind::Word || peek().kind == TokenKind::Symbol) && peek().text == text;
    }
    bool check_next(const std::string &text) const {
        return (peek(1).kind == TokenKind::Word || peek(1).kind == TokenKind::Symbol) && peek(1).text == text;
    }
    bool match(const std::string &text) { if (!check(text)) return false; ++current_; return true; }
    Token take(const std::string &text, const std::string &message) {
        if (!check(text)) fail(peek(), message);
        return tokens_[current_++];
    }
    Token word(const std::string &message) {
        if (peek().kind != TokenKind::Word) fail(peek(), message);
        return tokens_[current_++];
    }
    Token identifier(const std::string &message, bool allow_self = false) {
        Token token = word(message);
        static const std::unordered_set<std::string> reserved = {
            // 0.5
            "fn", "struct", "object", "sealed", "init", "let", "mut", "const", "return", "if", "else",
            "while", "for", "in", "break", "continue", "import", "true", "false", "unsafe", "raw", "public",
            "private", "protected", "self", "sizeof", "alignof", "contract", "meets", "extern", "native", "inject", "async", "await",
            "enum", "match", "case", "where",
            // 0.4 migration dialect
            "launch", "craft", "gives", "as", "shape", "bring", "done", "pin", "keep", "when",
            "otherwise", "whilst", "each", "from", "until", "leave", "next", "give", "say", "yes", "no",
            "and", "or", "not",
            // primitive spellings
            "int", "i64", "i32", "u64", "u32", "float", "f64", "f32", "bool", "str", "String", "nums", "void"};
        if (reserved.count(token.text) && !(allow_self && token.text == "self"))
            fail(token, "reserved word '" + token.text + "' cannot be a name");
        return token;
    }
    void separators() { while (match(";") || match(",")) {} }
    void end_statement() {
        if (!match(";") && !check("}") && !check("done") && !is_end())
            fail(peek(), "expected ';' or newline after statement");
        separators();
    }
    [[noreturn]] static void fail(const Token &token, const std::string &message,
                                  const std::string &help = {}) {
        throw Error(ppdiag::format(token.file, token.line, token.column, token.length,
                                   "E0100", message, message, help));
    }

    [[noreturn]] static void feature_pending(const Token &token, const std::string &feature,
                                             const std::string &milestone) {
        throw Error(ppdiag::format(token.file, token.line, token.column, token.length,
                                   "E0900", feature + " are reserved but not enabled",
                                   feature + " are reserved but not enabled",
                                   "This syntax is frozen for " + milestone + "; it is not executable yet."));
    }

    EnumDecl parse_enum() {
        take("enum", "expected 'enum'");
        EnumDecl result;
        result.token = identifier("expected enum name");
        result.name = result.token.text;
        result.generic_parameters = parse_generic_parameters();
        take("{", "expected '{' after enum name");
        separators();
        std::unordered_set<std::string> names;
        while (!check("}") && !is_end()) {
            EnumVariant variant;
            variant.token = identifier("expected enum variant name");
            variant.name = variant.token.text;
            if (!names.insert(variant.name).second) fail(variant.token, "duplicate enum variant '" + variant.name + "'");
            if (match("(")) {
                if (!check(")")) {
                    do variant.payload.push_back(parse_type()); while (match(","));
                }
                take(")", "expected ')' after variant payload");
            }
            result.variants.push_back(std::move(variant));
            separators();
        }
        take("}", "expected '}' after enum declaration");
        separators();
        if (result.variants.empty()) fail(result.token, "enum must declare at least one variant");
        return result;
    }

    static Type canonical_type(std::string name) {
        if (name == "int" || name == "i64" || name == "i32" || name == "u64" || name == "u32") return Type::Int;
        if (name == "float" || name == "f64" || name == "f32") return Type::Float;
        if (name == "bool") return Type::Bool;
        if (name == "str" || name == "String") return Type::Str;
        if (name == "nums") return Type::Nums;
        if (name == "void") return Type::Void;
        return Type{std::move(name)};
    }

    Type parse_type() {
        if (match("&")) {
            const bool mut = match("mut");
            const Type inner = parse_type();
            if (inner == Type::Void) fail(tokens_[current_ - 1], "reference target cannot be void");
            return Type{std::string(mut ? "&mut " : "&") + type_name(inner)};
        }
        if (match("*")) {
            (void)match("mut");  // raw pointers are mutable-capable; constness is expressed by API today
            const Type inner = parse_type();
            if (inner == Type::Void) fail(tokens_[current_ - 1], "raw pointer target cannot be void");
            return Type{"*" + type_name(inner)};
        }
        const Token token = word("expected a type");
        if (token.text == "inferred") fail(token, "'inferred' is not a type name");
        Type base = canonical_type(token.text);
        if (!match("<")) return base;
        std::string name = type_name(base) + "<";
        bool first = true;
        do {
            if (!first) name += ",";
            name += type_name(parse_type());
            first = false;
        } while (match(","));
        take_type_close("expected '>' after generic type arguments");
        return Type{std::move(name) + ">"};
    }

    void take_type_close(const std::string &message) {
        if (match(">")) return;
        if (check(">>")) {
            // The lexer keeps shift operators intact. In a type context, consume
            // one angle and leave the second for the enclosing generic type.
            Token &token = tokens_[current_];
            token.text = ">";
            ++token.offset;
            ++token.column;
            --token.length;
            return;
        }
        fail(peek(), message);
    }

    std::vector<GenericParameter> parse_generic_parameters() {
        std::vector<GenericParameter> parameters;
        if (!match("<")) return parameters;
        std::unordered_set<std::string> names;
        do {
            GenericParameter parameter;
            parameter.token = identifier("expected generic parameter name");
            parameter.name = parameter.token.text;
            if (!names.insert(parameter.name).second)
                fail(parameter.token, "duplicate generic parameter '" + parameter.name + "'");
            if (match(":")) {
                do parameter.constraints.push_back(parse_type()); while (match("+"));
            }
            parameters.push_back(std::move(parameter));
        } while (match(","));
        take_type_close("expected '>' after generic parameters");
        return parameters;
    }

    Visibility parse_visibility(Visibility fallback) {
        if (match("public")) return Visibility::Public;
        if (match("private")) return Visibility::Private;
        if (match("protected")) return Visibility::Protected;
        return fallback;
    }

    std::string parse_modern_import() {
        std::string name = word("expected a module name after 'import'").text;
        while (match("::") || match(".")) name += "/" + word("expected module segment").text;
        end_statement();
        return name;
    }
    std::string parse_legacy_import() {
        // `bring` is retained as PunPun's distinctive import spelling in the
        // 2026 grammar.  Dot-separated names remain accepted during the beta
        // migration, while `::` is the canonical module separator.
        std::string name = word("expected a module name after 'bring'").text;
        while (match("::") || match("."))
            name += "/" + word("expected a module segment after 'bring'").text;
        end_statement();
        return name;
    }

    // ---- modern declarations --------------------------------------------
    ForeignInjection parse_injection() {
        ForeignInjection injection;
        injection.token = take("@", "expected '@'");
        take("inject", "expected 'inject' after '@'");
        take("->", "expected '->' after '@inject'");
        injection.language = word("expected injection language after '@inject->'").text;
        take("(", "expected '(' after injection language");
        if (peek().kind != TokenKind::String) fail(peek(), "@inject source must be a string or triple-quoted string");
        injection.source_token = tokens_[current_++];
        injection.source = injection.source_token.text;
        take(")", "expected ')' after injected source");
        end_statement();
        return injection;
    }

    Function parse_extern_native() {
        take("extern", "expected 'extern'");
        take("native", "expected 'native' after 'extern'");
        take("fn", "expected 'fn' after 'extern native'");
        Function function;
        function.token = identifier("expected native function name");
        function.name = function.source_name = function.token.text;
        function.native_symbol = function.source_name;
        function.is_extern_native = true;
        parse_modern_parameter_list(function.parameters, false);
        if (match("->")) function.result = parse_type();
        if (!match(";")) fail(peek(), "extern native function declarations end with ';'");
        separators();
        return function;
    }

    Contract parse_contract() {
        take("contract", "expected 'contract'");
        Contract contract;
        contract.token = identifier("expected contract name");
        contract.name = contract.token.text;
        contract.generic_parameters = parse_generic_parameters();
        take("{", "expected '{' after contract name");
        separators();
        std::unordered_set<std::string> methods;
        while (!check("}") && !is_end()) {
            take("fn", "expected contract method declaration");
            ContractMethod method;
            method.token = identifier("expected contract method name");
            method.name = method.token.text;
            if (!methods.insert(method.name).second) fail(method.token, "duplicate contract method '" + method.name + "'");
            parse_modern_parameter_list(method.parameters, false);
            if (match("->")) method.result = parse_type();
            if (!match(";")) fail(peek(), "contract methods declare signatures and end with ';'");
            separators();
            contract.methods.push_back(std::move(method));
        }
        take("}", "expected '}' after contract declaration");
        separators();
        return contract;
    }

    void parse_modern_shape(Module &module) {
        bool sealed = match("sealed");
        const bool object = match("object");
        if (!object) take("struct", "expected 'struct' or 'object'");
        Shape shape;
        shape.reference_type = object;
        shape.sealed_type = sealed;
        shape.token = identifier("expected type name");
        shape.name = shape.token.text;
        shape.generic_parameters = parse_generic_parameters();
        if (match("meets")) {
            do shape.contracts.push_back(identifier("expected contract name after 'meets'").text); while (match(","));
        }
        take("{", "expected '{' after type name");
        separators();
        while (!check("}") && !is_end()) {
            Visibility visibility = parse_visibility(object ? Visibility::Private : Visibility::Public);
            if (check("fn") || check("async")) {
                const bool async_method = match("async");
                if (async_method && !check("fn")) fail(peek(), "expected 'fn' after 'async'");
                Function method = parse_modern_function(shape.name, false, visibility, object, async_method);
                shape.method_names.push_back(method.name);
                module.functions.push_back(std::move(method));
            } else if (match("init")) {
                if (!object) fail(tokens_[current_ - 1], "init is currently valid only inside object types");
                Function init = parse_initializer(shape.name, visibility);
                if (!shape.initializer_name.empty()) fail(init.token, "object can define only one init constructor");
                shape.initializer_name = init.name;
                shape.method_names.push_back(init.name);
                module.functions.push_back(std::move(init));
            } else if (object && (check("let") || check("const"))) {
                const Token keyword = tokens_[current_++];
                const bool mutable_field = keyword.text == "let" && match("mut");
                Parameter field;
                field.token = identifier("expected field name");
                field.name = field.token.text;
                field.mutable_value = mutable_field;
                field.visibility = visibility;
                take(":", "expected ':' after field name");
                field.type = parse_type();
                // Object fields are initialized by init. Field initializers are
                // intentionally deferred until definite-initialization lowering exists.
                if (match("=")) fail(peek(), "object field initializers belong in init for now");
                shape.fields.push_back(std::move(field));
                if (match(",")) separators(); else end_statement();
            } else {
                Parameter field;
                field.visibility = visibility;
                field.token = identifier("expected field, method, or constructor");
                field.name = field.token.text;
                field.mutable_value = true; // value-type fields can be changed through a mutable binding
                take(":", "expected ':' after field name");
                field.type = parse_type();
                shape.fields.push_back(std::move(field));
                if (match(",")) separators(); else end_statement();
            }
        }
        take("}", "expected '}' after type declaration");
        separators();
        module.shapes.push_back(std::move(shape));
    }

    Function parse_initializer(const std::string &owner, Visibility visibility) {
        Function function;
        function.token = tokens_[current_ - 1];
        function.source_name = "init";
        function.name = owner + "::init";
        function.owner_type = owner;
        function.is_method = true;
        function.is_initializer = true;
        function.self_mutable = true;
        function.visibility = visibility;
        Parameter self{function.token, "self", Type{owner}, true, Visibility::Private, {}};
        function.parameters.push_back(self);
        parse_modern_parameter_list(function.parameters, false);
        function.body = parse_modern_block();
        return function;
    }

    void parse_modern_parameter_list(std::vector<Parameter> &parameters, bool allow_explicit_self) {
        take("(", "expected '('");
        bool saw_default = false;
        if (!check(")")) {
            do {
                Parameter parameter;
                if (allow_explicit_self && (check("self") || (check("mut") && check_next("self")))) {
                    parameter.mutable_value = match("mut");
                    parameter.token = take("self", "expected self");
                    parameter.name = "self";
                    parameter.type = Type::Infer; // caller fills owner type
                } else {
                    parameter.mutable_value = match("mut");
                    parameter.token = identifier("expected parameter name");
                    parameter.name = parameter.token.text;
                    take(":", "expected ':' after parameter name");
                    parameter.type = parse_type();
                    if (parameter.type == Type::Void) fail(parameter.token, "parameter cannot have type void");
                    if (match("=")) {
                        std::unique_ptr<Expr> value = parse_expression();
                        if (!constant_default(*value))
                            fail(value->token, "default arguments must currently be literal constants");
                        parameter.default_value = std::shared_ptr<Expr>(value.release());
                        saw_default = true;
                    } else if (saw_default) {
                        fail(parameter.token, "required parameter cannot follow a default parameter");
                    }
                }
                parameters.push_back(std::move(parameter));
            } while (match(","));
        }
        take(")", "expected ')' after parameters");
    }

    static bool constant_default(const Expr &expression) {
        if (expression.kind == Expr::Kind::Integer || expression.kind == Expr::Kind::Float ||
            expression.kind == Expr::Kind::String || expression.kind == Expr::Kind::Boolean) return true;
        return expression.kind == Expr::Kind::Unary && expression.value == "-" &&
               expression.children.size() == 1 &&
               (expression.children[0]->kind == Expr::Kind::Integer || expression.children[0]->kind == Expr::Kind::Float);
    }

    void parse_call_arguments(Expr &call, bool receiver_present) {
        if (receiver_present) call.argument_names.push_back("");
        bool saw_named = false;
        if (!check(")")) {
            do {
                std::string name;
                if (peek().kind == TokenKind::Word && check_next(":")) {
                    name = identifier("expected argument name").text;
                    take(":", "expected ':' after argument name");
                    saw_named = true;
                } else if (saw_named) {
                    fail(peek(), "positional argument cannot follow a named argument");
                }
                call.children.push_back(parse_expression());
                call.argument_names.push_back(std::move(name));
            } while (match(","));
        }
        take(")", "expected ')' after arguments");
    }

    static bool expression_targets_self(const Expr *expression) {
        return expression && expression->kind == Expr::Kind::Member && !expression->children.empty() &&
               expression->children[0]->kind == Expr::Kind::Variable && expression->children[0]->value == "self";
    }

    static bool body_mutates_self(const std::vector<Stmt> &body) {
        for (const Stmt &statement : body) {
            if (statement.kind == Stmt::Kind::Assign && expression_targets_self(statement.target.get())) return true;
            if (body_mutates_self(statement.body) || body_mutates_self(statement.alternative)) return true;
        }
        return false;
    }

    Function parse_modern_function(const std::string &owner, bool initializer, Visibility visibility, bool owner_is_object, bool async_function = false) {
        (void)initializer;
        take("fn", "expected 'fn'");
        Function function;
        function.token = identifier("expected function name");
        function.source_name = function.token.text;
        function.generic_parameters = parse_generic_parameters();
        function.owner_type = owner;
        function.is_method = !owner.empty();
        function.visibility = visibility;
        function.is_async = async_function;
        function.name = owner.empty() ? function.source_name : owner + "::" + function.source_name;

        std::vector<Parameter> params;
        parse_modern_parameter_list(params, !owner.empty());
        const bool explicit_self = !owner.empty() && !params.empty() && params.front().name == "self";
        if (match("->")) function.result = parse_type();
        function.body = parse_modern_block();

        if (!owner.empty()) {
            bool mutable_self = explicit_self ? params.front().mutable_value : body_mutates_self(function.body);
            function.self_mutable = mutable_self;
            if (explicit_self) {
                params.front().type = owner_is_object ? Type{owner} : Type{std::string(mutable_self ? "&mut " : "&") + owner};
            } else {
                // PunPun keeps the receiver implicit for ordinary OOP ergonomics.
                // Explicit `self` / `mut self` remains accepted when ownership
                // intent needs to be stated by the programmer.
                Parameter self{function.token, "self",
                    owner_is_object ? Type{owner} : Type{std::string(mutable_self ? "&mut " : "&") + owner},
                    mutable_self, Visibility::Private, {}};
                params.insert(params.begin(), std::move(self));
            }
        }
        function.parameters = std::move(params);
        return function;
    }

    std::vector<Stmt> parse_modern_block() {
        take("{", "expected '{' to open block");
        separators();
        std::vector<Stmt> statements;
        while (!check("}") && !is_end()) statements.push_back(parse_statement());
        take("}", "expected '}' after block");
        separators();
        return statements;
    }

    // ---- 0.4 migration declarations -------------------------------------
    Shape parse_legacy_shape() {
        Shape shape;
        shape.token = identifier("expected shape name");
        shape.name = shape.token.text;
        take(":", "expected ':' after shape name");
        separators();
        while (!check("done") && !is_end()) {
            Parameter field;
            field.token = identifier("expected field name");
            field.name = field.token.text;
            field.mutable_value = true;
            take("as", "expected 'as' before field type");
            field.type = parse_type();
            shape.fields.push_back(std::move(field));
            end_statement();
        }
        take("done", "expected 'done' after shape");
        end_statement();
        return shape;
    }

    Function parse_legacy_function() {
        Function function;
        if (match("launch")) {
            function.token = tokens_[current_ - 1];
            function.name = function.source_name = "main";
            function.result = Type::Int;
            // Structured `launch { ... }` is the canonical PunPun entry point.
            // `launch: ... done` remains accepted only as a 0.5 beta migration
            // path so existing projects can be converted with `pp migrate`.
            function.body = check("{") ? parse_modern_block() : parse_legacy_block();
            return function;
        }
        take("craft", "expected declaration: fn, struct, object, import, craft, launch, shape, or bring");
        function.token = identifier("expected craft name");
        function.name = function.source_name = function.token.text;
        if (function.name == "main") fail(function.token, "legacy entry point uses 'launch:'; modern code uses 'fn main() { ... }'");
        take("(", "expected '(' after function name");
        if (!check(")")) {
            do {
                Parameter parameter;
                parameter.token = identifier("expected parameter name");
                parameter.name = parameter.token.text;
                take("as", "expected 'as' after parameter name");
                parameter.type = parse_type();
                if (parameter.type == Type::Void) fail(parameter.token, "parameter cannot have type void");
                function.parameters.push_back(parameter);
            } while (match(","));
        }
        take(")", "expected ')' after parameters");
        if (match("gives")) function.result = parse_type();
        function.body = parse_legacy_block();
        return function;
    }

    std::vector<Stmt> parse_legacy_block() {
        take(":", "expected ':' to open legacy block");
        separators();
        std::vector<Stmt> statements;
        while (!check("done") && !is_end()) statements.push_back(parse_statement());
        take("done", "expected 'done' after block");
        end_statement();
        return statements;
    }

    // ---- statements shared by both dialects -----------------------------
    Stmt parse_statement() {
        if (check("let") || check("const")) {
            const Token keyword = tokens_[current_++];
            Stmt statement{Stmt::Kind::Variable, keyword};
            statement.constant_value = keyword.text == "const";
            statement.mutable_value = !statement.constant_value && match("mut");
            const Token name = identifier("expected variable name");
            statement.name = name.text;
            if (match(":")) statement.declared_type = parse_type();
            if (statement.declared_type == Type::Void) fail(name, "variable cannot have type void");
            take("=", "variable declarations require '=' and an initializer");
            statement.expression = parse_expression();
            end_statement();
            return statement;
        }
        if (check("pin") || check("keep")) {
            const Token keyword = tokens_[current_++];
            Stmt statement{Stmt::Kind::Variable, keyword};
            statement.mutable_value = keyword.text == "keep";
            const Token name = identifier("expected variable name");
            statement.name = name.text;
            if (match("as")) statement.declared_type = parse_type();
            if (statement.declared_type == Type::Void) fail(name, "variable cannot have type void");
            take("<-", "bindings must be initialized with '<-'");
            statement.expression = parse_expression();
            end_statement();
            return statement;
        }
        if (match("return") || match("give")) {
            Stmt statement{Stmt::Kind::Return, tokens_[current_ - 1]};
            if (!check(";") && !check("}") && !check("done")) statement.expression = parse_expression();
            end_statement();
            return statement;
        }
        if (match("if")) {
            Stmt statement{Stmt::Kind::If, tokens_[current_ - 1]};
            statement.expression = parse_expression();
            statement.body = parse_modern_block();
            if (match("else")) statement.alternative = parse_modern_block();
            return statement;
        }
        if (match("when")) {
            Stmt statement{Stmt::Kind::If, tokens_[current_ - 1]};
            statement.expression = parse_expression();
            take(":", "expected ':' after when condition");
            separators();
            while (!check("otherwise") && !check("done") && !is_end()) statement.body.push_back(parse_statement());
            if (match("otherwise")) {
                take(":", "expected ':' after otherwise"); separators();
                while (!check("done") && !is_end()) statement.alternative.push_back(parse_statement());
            }
            take("done", "expected 'done' after when"); end_statement(); return statement;
        }
        if (match("while")) {
            Stmt statement{Stmt::Kind::While, tokens_[current_ - 1]};
            statement.expression = parse_expression();
            statement.body = parse_modern_block();
            return statement;
        }
        if (match("whilst")) {
            Stmt statement{Stmt::Kind::While, tokens_[current_ - 1]};
            statement.expression = parse_expression();
            statement.body = parse_legacy_block();
            return statement;
        }
        if (match("for")) {
            Stmt statement{Stmt::Kind::Each, tokens_[current_ - 1]};
            statement.name = identifier("expected range binding").text;
            take("in", "expected 'in' after range binding");
            statement.expression = parse_expression();
            take("..", "expected '..' in range");
            statement.upper = parse_expression();
            statement.body = parse_modern_block();
            return statement;
        }
        if (match("each")) {
            Stmt statement{Stmt::Kind::Each, tokens_[current_ - 1]};
            statement.name = identifier("expected range binding").text;
            take("from", "expected 'from' in range");
            statement.expression = parse_expression();
            take("until", "expected 'until' in range");
            statement.upper = parse_expression();
            statement.body = parse_legacy_block();
            return statement;
        }
        if (check("break") || check("continue") || check("leave") || check("next")) {
            Token token = tokens_[current_++];
            const bool is_break = token.text == "break" || token.text == "leave";
            Stmt statement{is_break ? Stmt::Kind::Break : Stmt::Kind::Continue, token};
            end_statement();
            return statement;
        }
        if (match("unsafe")) {
            Stmt statement{Stmt::Kind::Unsafe, tokens_[current_ - 1]};
            statement.body = parse_modern_block();
            return statement;
        }
        if (match("say")) {
            Stmt statement{Stmt::Kind::Say, tokens_[current_ - 1]};
            statement.expression = parse_expression();
            end_statement();
            return statement;
        }

        Stmt statement{Stmt::Kind::Expression, peek()};
        statement.expression = parse_expression();
        if (check("=") || check("<-") || check("+=") || check("-=") || check("*=") || check("/=")) {
            statement.kind = Stmt::Kind::Assign;
            statement.assignment_op = tokens_[current_++].text;
            statement.target = std::move(statement.expression);
            statement.expression = parse_expression();
        }
        end_statement();
        return statement;
    }

    // ---- expressions -----------------------------------------------------
    std::unique_ptr<Expr> parse_expression() { return parse_binary(1); }
    static int precedence(const std::string &op) {
        if (op == "or" || op == "||") return 1;
        if (op == "and" || op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
        if (op == "<<" || op == ">>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        return 0;
    }
    static std::string normalize_operator(std::string op) {
        if (op == "&&") return "and";
        if (op == "||") return "or";
        if (op == "!") return "not";
        return op;
    }
    std::unique_ptr<Expr> parse_binary(int minimum) {
        auto left = parse_unary();
        while (precedence(peek().text) >= minimum) {
            Token op = tokens_[current_++];
            const int level = precedence(op.text);
            auto right = parse_binary(level + 1);
            auto combined = std::make_unique<Expr>();
            combined->kind = Expr::Kind::Binary;
            combined->token = op;
            combined->value = normalize_operator(op.text);
            combined->children.push_back(std::move(left));
            combined->children.push_back(std::move(right));
            left = std::move(combined);
        }
        return left;
    }

    std::unique_ptr<Expr> parse_unary() {
        if (check("await") || check("not") || check("!") || check("-") || check("~") || check("&") || check("*")) {
            Token op = tokens_[current_++];
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Unary;
            expression->token = op;
            if (op.text == "&" && match("mut")) expression->value = "&mut";
            else if (op.text == "&" && match("raw")) expression->value = "&raw";
            else expression->value = normalize_operator(op.text);
            expression->children.push_back(parse_unary());
            return expression;
        }
        auto expression = parse_primary();
        for (;;) {
            if (match(".")) {
                const Token dot = tokens_[current_ - 1];
                const Token member = identifier("expected field or method name");
                if (match("(")) {
                    auto call = std::make_unique<Expr>();
                    call->kind = Expr::Kind::MethodCall;
                    call->token = member;
                    call->value = member.text;
                    call->children.push_back(std::move(expression));
                    parse_call_arguments(*call, true);
                    expression = std::move(call);
                } else {
                    auto access = std::make_unique<Expr>();
                    access->kind = Expr::Kind::Member;
                    access->token = dot;
                    access->value = member.text;
                    access->children.push_back(std::move(expression));
                    expression = std::move(access);
                }
            } else if (match("[")) {
                auto access = std::make_unique<Expr>();
                access->kind = Expr::Kind::Index;
                access->token = tokens_[current_ - 1];
                access->children.push_back(std::move(expression));
                access->children.push_back(parse_expression());
                take("]", "expected ']' after index");
                expression = std::move(access);
            } else if (match("?")) {
                auto propagated = std::make_unique<Expr>();
                propagated->kind = Expr::Kind::Propagate;
                propagated->token = tokens_[current_ - 1];
                propagated->children.push_back(std::move(expression));
                expression = std::move(propagated);
            } else break;
        }
        return expression;
    }

    std::unique_ptr<Expr> parse_primary() {
        if (is_end()) fail(peek(), "expected expression");
        const Token token = tokens_[current_++];
        auto expression = std::make_unique<Expr>();
        expression->token = token;
        expression->value = token.text;

        if (token.text == "match") return parse_match_expression(token);

        if (token.kind == TokenKind::Integer) expression->kind = Expr::Kind::Integer;
        else if (token.kind == TokenKind::Float) expression->kind = Expr::Kind::Float;
        else if (token.kind == TokenKind::String) expression->kind = Expr::Kind::String;
        else if (token.text == "yes" || token.text == "no" || token.text == "true" || token.text == "false") {
            expression->kind = Expr::Kind::Boolean;
            expression->value = (token.text == "yes" || token.text == "true") ? "yes" : "no";
        } else if ((token.text == "sizeof" || token.text == "alignof") && match("<")) {
            const Type type = parse_type();
            take(">", "expected '>' after intrinsic type");
            take("(", "expected '(' after intrinsic type");
            take(")", "expected ')' after intrinsic");
            expression->kind = token.text == "sizeof" ? Expr::Kind::SizeOf : Expr::Kind::AlignOf;
            expression->value = type.name;
        } else if (token.kind == TokenKind::Word) {
            std::string qualified = token.text;
            if (looks_like_generic_call()) {
                take("<", "expected '<'");
                do expression->type_arguments.push_back(parse_type()); while (match(","));
                take_type_close("expected '>' after explicit type arguments");
            }
            while (match("::")) {
                qualified += "::" + identifier("expected qualified name segment").text;
            }
            expression->value = qualified;
            if (match("(")) {
                expression->kind = Expr::Kind::Call;
                parse_call_arguments(*expression, false);
            } else expression->kind = Expr::Kind::Variable;
        } else if (token.text == "[") {
            expression->kind = Expr::Kind::List;
            if (!check("]")) {
                do expression->children.push_back(parse_expression()); while (match(","));
            }
            take("]", "expected ']' after list literal");
        } else if (token.text == "(") {
            expression = parse_expression();
            take(")", "expected ')' after expression");
        } else fail(token, "expected expression");
        return expression;
    }

    bool looks_like_generic_call() const {
        if (!check("<")) return false;
        int depth = 0;
        for (std::size_t i = current_; i < tokens_.size(); ++i) {
            const std::string &text = tokens_[i].text;
            if (text == "<") ++depth;
            else if (text == ">") {
                if (--depth == 0) return i + 1 < tokens_.size() &&
                    (tokens_[i + 1].text == "(" || tokens_[i + 1].text == "::");
            } else if (text == ">>") {
                depth -= 2;
                if (depth <= 0) return i + 1 < tokens_.size() &&
                    (tokens_[i + 1].text == "(" || tokens_[i + 1].text == "::");
            } else if (depth > 0 && (text == ";" || text == "{" || text == "=" || text == "=>")) return false;
        }
        return false;
    }

    Pattern parse_pattern() {
        Pattern pattern;
        pattern.token = peek();
        if (match("_")) { pattern.kind = Pattern::Kind::Wildcard; pattern.value = "_"; return pattern; }
        if (peek().kind == TokenKind::Integer) {
            pattern.kind = Pattern::Kind::Integer; pattern.value = tokens_[current_++].text; return pattern;
        }
        if (peek().kind == TokenKind::String) {
            pattern.kind = Pattern::Kind::String; pattern.value = tokens_[current_++].text; return pattern;
        }
        if (check("true") || check("false") || check("yes") || check("no")) {
            pattern.kind = Pattern::Kind::Boolean;
            if (match("true") || match("yes")) pattern.value = "yes";
            else { ++current_; pattern.value = "no"; }
            return pattern;
        }
        Token first = identifier("expected pattern");
        std::string name = first.text;
        while (match("::")) name += "::" + identifier("expected variant name").text;
        const bool variant = name.find("::") != std::string::npos || (!name.empty() && std::isupper(static_cast<unsigned char>(name[0])));
        pattern.kind = variant ? Pattern::Kind::Variant : Pattern::Kind::Binding;
        pattern.value = name;
        if (match("(")) {
            pattern.kind = Pattern::Kind::Variant;
            if (!check(")")) do pattern.children.push_back(parse_pattern()); while (match(","));
            take(")", "expected ')' after variant pattern");
        }
        return pattern;
    }

    std::unique_ptr<Expr> parse_match_expression(const Token &token) {
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Match;
        result->token = token;
        result->children.push_back(parse_expression());
        take("{", "expected '{' after match value");
        separators();
        while (!check("}") && !is_end()) {
            result->match_patterns.push_back(parse_pattern());
            take("=>", "expected '=>' after match pattern");
            result->children.push_back(parse_expression());
            separators();
        }
        take("}", "expected '}' after match expression");
        if (result->match_patterns.empty()) fail(token, "match must contain at least one arm");
        return result;
    }
};

#endif  // PUNPUN_FRONTEND_HPP
