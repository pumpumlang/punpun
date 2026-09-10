#ifndef PPC_SYNTAX_LEXER_HPP
#define PPC_SYNTAX_LEXER_HPP

#include <vector>

#include "ppc/support/diagnostic.hpp"
#include "ppc/support/source.hpp"
#include "ppc/syntax/token.hpp"

namespace ppc {

/// Converts source text into a flat token vector.
///
/// The one structural rule worth knowing: a newline emits a synthetic `;`
/// unless the lexer is inside `(` or `[`. Braces do *not* suppress it, because
/// the modern grammar still terminates statements at end of line. That is what
/// lets `let x = 1` parse without a trailing semicolon while `foo(a,\n b)`
/// keeps working across lines.
class Lexer {
  public:
    Lexer(const SourceManager &sources, DiagnosticEngine &diagnostics, Interner &interner)
        : sources_(sources), diagnostics_(diagnostics), interner_(interner) {}

    /// Tokenizes one file. The returned vector always ends with Tok::End, even
    /// after a lexical error, so the parser never needs a bounds check.
    std::vector<Token> tokenize(FileId file);

  private:
    const SourceManager &sources_;
    DiagnosticEngine &diagnostics_;
    Interner &interner_;

    // Per-run state.
    FileId file_;
    std::string_view text_;
    u32 index_ = 0;
    u32 nesting_ = 0;  // depth of ( and [ only
    std::vector<Token> output_;

    bool at_end() const { return index_ >= text_.size(); }
    char peek(u32 offset = 0) const {
        return (index_ + offset < text_.size()) ? text_[index_ + offset] : '\0';
    }
    char advance() { return text_[index_++]; }
    bool eat(char c) {
        if (peek() == c) { ++index_; return true; }
        return false;
    }
    Span span_from(u32 start) const { return Span{file_, start, index_}; }

    void skip_trivia();
    void push(Tok kind, u32 start);
    void lex_word(u32 start);
    void lex_number(u32 start);
    void lex_string(u32 start);
    void lex_punctuation(u32 start);
};

}  // namespace ppc

#endif
