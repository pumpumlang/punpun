#ifndef PPC_SYNTAX_PARSER_HPP
#define PPC_SYNTAX_PARSER_HPP

#include <vector>

#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"
#include "ppc/syntax/ast.hpp"
#include "ppc/syntax/token.hpp"

namespace ppc {

/// Recursive-descent parser with Pratt-style binary expression parsing.
///
/// Both PunPun dialects are handled by one parser producing one AST. Where the
/// grammars differ only in spelling (`fn`/`craft`, `if`/`when`, `{}`/`:`+`done`)
/// the difference is absorbed at the token level, so no later phase can tell
/// which dialect a declaration came from.
class Parser {
  public:
    Parser(std::vector<Token> tokens, Arena &arena, Interner &interner,
           DiagnosticEngine &diagnostics)
        : tokens_(std::move(tokens)), arena_(arena), interner_(interner),
          diagnostics_(diagnostics) {}

    /// Parses one file. On error the parser recovers at the next declaration
    /// boundary and keeps going, so a single run reports many problems.
    Module *parse(FileId file, const std::string &path);

  private:
    /// Which bracket style a block opened with, so it can be closed correctly.
    enum class BlockStyle { Brace, Legacy };

    std::vector<Token> tokens_;
    Arena &arena_;
    Interner &interner_;
    /// Module currently being parsed, so a lambda can be lifted into it.
    Module *module_ = nullptr;
    /// Counter for generated lambda names, unique within a module.
    u32 lambda_serial_ = 0;
    DiagnosticEngine &diagnostics_;
    std::size_t index_ = 0;
    /// Depth of loop nesting, used to reject `break` outside a loop early.
    int loop_depth_ = 0;
    bool panic_mode_ = false;

    // -- token helpers ------------------------------------------------------
    const Token &peek(std::size_t offset = 0) const;
    const Token &previous() const;
    bool check(Tok kind) const { return peek().kind == kind; }
    bool check_next(Tok kind) const { return peek(1).kind == kind; }
    bool at_end() const { return peek().kind == Tok::End; }
    Token advance();
    bool match(Tok kind);
    Token expect(Tok kind, const char *what);
    /// Consumes any run of separators (newline-semicolons, real semicolons,
    /// and commas where the grammar allows them as separators).
    void skip_separators();
    void end_statement();
    Symbol expect_identifier(const char *what, bool allow_self = false);

    void error_at(const Token &token, Code code, const std::string &message,
                  const std::string &help = {});
    /// Skips ahead to a plausible restart point after a parse error.
    void synchronize();

    // -- declarations -------------------------------------------------------
    void parse_declaration(Module &module);
    ImportDecl parse_import();
    InjectionDecl parse_injection();
    FunctionDecl *parse_extern_native();
    FunctionDecl *parse_function(Symbol owner, bool is_method, Visibility visibility,
                                 bool is_async, bool is_initializer);
    FunctionDecl *parse_legacy_function();
    FunctionDecl *parse_launch();
    ShapeDecl *parse_shape(bool is_reference, bool is_sealed);
    ShapeDecl *parse_legacy_shape();
    EnumDecl *parse_enum();
    ContractDecl *parse_contract();

    std::vector<GenericParam> parse_generic_params();
    void parse_param_list(std::vector<Param> &out, FunctionDecl *owner);
    void parse_shape_body(ShapeDecl &shape);

    // -- statements ---------------------------------------------------------
    BlockStyle open_block(const char *what);
    bool at_block_end(BlockStyle style) const;
    void close_block(BlockStyle style, const char *what);
    /// Parses statements up to the block terminator without consuming it.
    std::vector<Stmt *> parse_block_body(BlockStyle style);
    std::vector<Stmt *> parse_block(const char *what);
    /// True when `kind` is the next token ignoring any run of separators, and
    /// consumes those separators plus the token when it matches. Needed because
    /// `}` and `else` are often on separate lines, and the lexer turns the
    /// newline between them into a statement separator.
    bool match_across_separators(Tok kind);

    Stmt *parse_statement();
    Stmt *parse_let(bool is_mutable, bool is_const, bool legacy);
    Stmt *parse_if();
    Stmt *parse_while();
    Stmt *parse_for();
    Stmt *parse_each();
    Stmt *parse_return(bool legacy);
    Stmt *parse_say();
    Stmt *parse_unsafe();
    Stmt *parse_expression_statement();

    // -- expressions --------------------------------------------------------
    Expr *parse_expression();
    Expr *parse_binary(int min_precedence);
    Expr *parse_unary();
    Expr *parse_postfix(Expr *base);
    Expr *parse_primary();
    Expr *parse_lambda();
    Expr *parse_match();
    Expr *parse_list_literal();
    void parse_arguments(Expr &call);

    // -- patterns and types -------------------------------------------------
    Pattern *parse_pattern();
    TypeExpr *parse_type();
    TypeExpr *make_infer(Span span);

    // -- node construction --------------------------------------------------
    Expr *make_expr(Expr::Kind kind, Span span);
    Stmt *make_stmt(Stmt::Kind kind, Span span);
};

}  // namespace ppc

#endif
