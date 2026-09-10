#ifndef PPC_SYNTAX_TOKEN_HPP
#define PPC_SYNTAX_TOKEN_HPP

#include <string>

#include "ppc/support/common.hpp"

namespace ppc {

/// Every keyword in both dialects, as an X-macro so the enum, the name table,
/// and the lookup map can never drift apart.
///
/// PunPun accepts two surface syntaxes that lower to one AST: the modern
/// brace/semicolon grammar (`fn`, `let`, `if`) and the 0.4 migration dialect
/// (`craft`, `keep`, `when`). Both sets are live keywords; the parser decides
/// which production to run from the leading token, not from a file-level mode
/// switch, so a project mid-migration can mix files freely.
#define PPC_KEYWORDS(X)                                                       \
    /* modern declarations */                                                 \
    X(Fn, "fn") X(Struct, "struct") X(Object, "object") X(Sealed, "sealed")   \
    X(Init, "init") X(Enum, "enum") X(Contract, "contract") X(Meets, "meets") \
    X(Import, "import") X(Extern, "extern") X(Native, "native")               \
    X(Inject, "inject") X(Async, "async") X(Await, "await")                   \
    /* modern statements */                                                   \
    X(Let, "let") X(Mut, "mut") X(Const, "const") X(Return, "return")         \
    X(If, "if") X(Else, "else") X(While, "while") X(For, "for") X(In, "in")   \
    X(Break, "break") X(Continue, "continue") X(Match, "match")               \
    X(Case, "case") X(Where, "where") X(Unsafe, "unsafe") X(Raw, "raw")       \
    X(Move, "move") X(SizeOf, "sizeof") X(AlignOf, "alignof")                 \
    /* visibility and receiver */                                             \
    X(Public, "public") X(Private, "private") X(Protected, "protected")       \
    X(SelfKw, "self")                                                         \
    /* migration dialect */                                                   \
    X(Launch, "launch") X(Craft, "craft") X(Gives, "gives") X(As, "as")       \
    X(Shape, "shape") X(Bring, "bring") X(Done, "done") X(Pin, "pin")         \
    X(Keep, "keep") X(When, "when") X(Otherwise, "otherwise")                 \
    X(Whilst, "whilst") X(Each, "each") X(From, "from") X(Until, "until")     \
    X(Leave, "leave") X(Next, "next") X(Give, "give") X(Say, "say")           \
    /* literals and word operators */                                         \
    X(True, "true") X(False, "false") X(Yes, "yes") X(No, "no")               \
    X(And, "and") X(Or, "or") X(Not, "not")

/// Punctuation, longest-match first within each starting character.
#define PPC_PUNCTUATION(X)                                                    \
    X(Arrow, "->") X(FatArrow, "=>") X(LeftArrow, "<-") X(ColonColon, "::")   \
    X(EqualEqual, "==") X(BangEqual, "!=") X(LessEqual, "<=")                 \
    X(GreaterEqual, ">=") X(AmpAmp, "&&") X(PipePipe, "||") X(DotDot, "..")   \
    X(PlusEqual, "+=") X(MinusEqual, "-=") X(StarEqual, "*=")                 \
    X(SlashEqual, "/=") X(PercentEqual, "%=") X(ShiftLeft, "<<")              \
    X(ShiftRight, ">>")                                                       \
    X(LeftParen, "(") X(RightParen, ")") X(LeftBracket, "[")                  \
    X(RightBracket, "]") X(LeftBrace, "{") X(RightBrace, "}")                 \
    X(Colon, ":") X(Semicolon, ";") X(Comma, ",") X(Dot, ".")                 \
    X(Plus, "+") X(Minus, "-") X(Star, "*") X(Slash, "/") X(Percent, "%")     \
    X(Equal, "=") X(Less, "<") X(Greater, ">") X(Bang, "!") X(Amp, "&")       \
    X(Pipe, "|") X(Caret, "^") X(Tilde, "~") X(At, "@") X(Question, "?")

enum class Tok : u16 {
    End = 0,
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
#define PPC_DECLARE_KEYWORD(name, text) name,
    PPC_KEYWORDS(PPC_DECLARE_KEYWORD)
#undef PPC_DECLARE_KEYWORD
#define PPC_DECLARE_PUNCT(name, text) name,
        PPC_PUNCTUATION(PPC_DECLARE_PUNCT)
#undef PPC_DECLARE_PUNCT
            TokenCount
};

/// Display spelling, used in "expected X, found Y" messages.
const char *token_spelling(Tok kind);

/// Maps an identifier to its keyword kind, or Tok::Identifier when it is a
/// plain name. Backed by a static hash map built once per process.
Tok keyword_lookup(std::string_view text);

/// True for words that cannot be used as user-defined names. This mirrors the
/// reserved set in the reference implementation, including primitive spellings
/// like `i64` that are legal in type position but not as identifiers.
bool is_reserved_word(std::string_view text);

struct Token {
    Tok kind = Tok::End;
    Span span;
    /// Identifier text, or the decoded value of a string literal.
    Symbol text;
    /// Literal payloads. Only one is meaningful, selected by `kind`.
    i64 int_value = 0;
    double float_value = 0.0;
    /// True when this `;` came from a newline rather than a literal semicolon.
    /// The parser uses it to accept `}` or `done` without a preceding separator.
    bool synthetic = false;

    bool is(Tok k) const { return kind == k; }
};

}  // namespace ppc

#endif
