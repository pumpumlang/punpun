#include "ppc/syntax/lexer.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace ppc {

// ---------------------------------------------------------------------------
// Token tables
// ---------------------------------------------------------------------------

const char *token_spelling(Tok kind) {
    switch (kind) {
        case Tok::End: return "end of file";
        case Tok::Identifier: return "identifier";
        case Tok::IntLiteral: return "integer literal";
        case Tok::FloatLiteral: return "float literal";
        case Tok::StringLiteral: return "string literal";
#define PPC_SPELL_KEYWORD(name, spelling) case Tok::name: return spelling;
            PPC_KEYWORDS(PPC_SPELL_KEYWORD)
            PPC_PUNCTUATION(PPC_SPELL_KEYWORD)
#undef PPC_SPELL_KEYWORD
        default: return "<token>";
    }
}

namespace {

const std::unordered_map<std::string_view, Tok> &keyword_table() {
    static const std::unordered_map<std::string_view, Tok> table = {
#define PPC_MAP_KEYWORD(name, spelling) {spelling, Tok::name},
        PPC_KEYWORDS(PPC_MAP_KEYWORD)
#undef PPC_MAP_KEYWORD
    };
    return table;
}

}  // namespace

Tok keyword_lookup(std::string_view text) {
    const auto &table = keyword_table();
    auto it = table.find(text);
    return it == table.end() ? Tok::Identifier : it->second;
}

bool is_reserved_word(std::string_view text) {
    if (keyword_lookup(text) != Tok::Identifier) return true;
    // Primitive spellings are not keywords (they appear in type position and are
    // parsed as named types) but are still refused as user-defined names, which
    // matches the reference frontend's reserved set.
    static const std::unordered_map<std::string_view, bool> primitives = {
        {"int", true},  {"i64", true},  {"i32", true},   {"u64", true},
        {"u32", true},  {"float", true},{"f64", true},   {"f32", true},
        {"bool", true}, {"str", true},  {"String", true},{"text", true},
        {"nums", true}, {"void", true}};
    return primitives.count(text) != 0;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

std::vector<Token> Lexer::tokenize(FileId file) {
    file_ = file;
    text_ = sources_.file(file).text;
    index_ = 0;
    nesting_ = 0;
    output_.clear();
    output_.reserve(text_.size() / 4 + 16);

    while (true) {
        skip_trivia();
        if (at_end()) break;

        const u32 start = index_;
        const char c = peek();

        if (c == '\n') {
            ++index_;
            // A newline ends a statement unless we are mid-expression inside
            // parentheses or brackets.
            if (nesting_ == 0) {
                Token token;
                token.kind = Tok::Semicolon;
                token.span = span_from(start);
                token.synthetic = true;
                output_.push_back(token);
            }
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            lex_word(start);
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            lex_number(start);
        } else if (c == '"') {
            lex_string(start);
        } else {
            lex_punctuation(start);
        }
    }

    Token end;
    end.kind = Tok::End;
    end.span = Span{file_, index_, index_};
    output_.push_back(end);
    return std::move(output_);
}

void Lexer::skip_trivia() {
    while (!at_end()) {
        const char c = peek();
        // Newlines are significant, so they are not trivia.
        if (c != '\n' && std::isspace(static_cast<unsigned char>(c))) {
            ++index_;
            continue;
        }
        // Both comment spellings run to end of line. `#` is the migration
        // dialect's form and `//` the modern one; both files may appear in one
        // project, so both are always accepted.
        if (c == '#' || (c == '/' && peek(1) == '/')) {
            while (!at_end() && peek() != '\n') ++index_;
            continue;
        }
        // Block comments nest, so a commented-out region containing another
        // comment does not terminate early.
        if (c == '/' && peek(1) == '*') {
            index_ += 2;
            u32 depth = 1;
            while (!at_end() && depth > 0) {
                if (peek() == '/' && peek(1) == '*') { index_ += 2; ++depth; }
                else if (peek() == '*' && peek(1) == '/') { index_ += 2; --depth; }
                else ++index_;
            }
            continue;
        }
        break;
    }
}

void Lexer::push(Tok kind, u32 start) {
    Token token;
    token.kind = kind;
    token.span = span_from(start);
    output_.push_back(token);
}

void Lexer::lex_word(u32 start) {
    while (!at_end() &&
           (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        ++index_;
    }
    const std::string_view word = text_.substr(start, index_ - start);

    Token token;
    token.kind = keyword_lookup(word);
    token.span = span_from(start);
    token.text = interner_.intern(word);

    // `yes`/`no` are the migration dialect's booleans; normalizing them here
    // means no later phase has to know they existed.
    if (token.kind == Tok::Yes) token.kind = Tok::True;
    if (token.kind == Tok::No) token.kind = Tok::False;

    output_.push_back(token);
}

void Lexer::lex_number(u32 start) {
    while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
        ++index_;
    }

    bool is_float = false;
    // A `.` only starts a fraction when a digit follows. This is what keeps
    // `0..10` lexing as a range rather than as the float `0.` followed by junk.
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        ++index_;
        while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
            ++index_;
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        const u32 save = index_;
        ++index_;
        if (peek() == '+' || peek() == '-') ++index_;
        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            is_float = true;
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) ++index_;
        } else {
            index_ = save;  // not an exponent after all
        }
    }

    std::string digits;
    digits.reserve(index_ - start);
    for (u32 i = start; i < index_; ++i) {
        if (text_[i] != '_') digits += text_[i];
    }

    Token token;
    token.span = span_from(start);
    if (is_float) {
        token.kind = Tok::FloatLiteral;
        token.float_value = std::strtod(digits.c_str(), nullptr);
    } else {
        token.kind = Tok::IntLiteral;
        errno = 0;
        char *stop = nullptr;
        const long long parsed = std::strtoll(digits.c_str(), &stop, 10);
        if (errno == ERANGE) {
            diagnostics_.error(Code::InvalidNumber, "integer literal does not fit in i64")
                .label(token.span)
                .with_help("the range of i64 is -9223372036854775808 to 9223372036854775807");
        }
        token.int_value = static_cast<i64>(parsed);
    }
    output_.push_back(token);
}

void Lexer::lex_string(u32 start) {
    ++index_;  // opening quote

    // Triple-quoted literals keep newlines verbatim. They exist mainly for
    // @inject blocks, but they are ordinary string tokens so no second lexer is
    // needed anywhere downstream.
    if (peek() == '"' && peek(1) == '"') {
        index_ += 2;
        std::string value;
        bool closed = false;
        while (!at_end()) {
            if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
                index_ += 3;
                closed = true;
                break;
            }
            value += advance();
        }
        Token token;
        token.kind = Tok::StringLiteral;
        token.span = span_from(start);
        token.text = interner_.intern(value);
        if (!closed) {
            diagnostics_.error(Code::UnterminatedString, "unterminated triple-quoted string")
                .label(token.span)
                .with_help("close the literal with \"\"\"");
        }
        output_.push_back(token);
        return;
    }

    std::string value;
    bool closed = false;
    while (!at_end()) {
        const char c = peek();
        if (c == '"') { ++index_; closed = true; break; }
        if (c == '\n') break;  // unterminated; report at the opening quote
        if (c == '\\') {
            const u32 escape_start = index_;
            ++index_;
            if (at_end()) break;
            const char escaped = advance();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '0': value += '\0'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default:
                    diagnostics_
                        .error(Code::UnknownEscape,
                               std::string("unknown string escape '\\") + escaped + "'")
                        .label(Span{file_, escape_start, index_})
                        .with_help("supported escapes are \\n \\t \\r \\0 \\\" and \\\\");
                    value += escaped;
            }
            continue;
        }
        value += advance();
    }

    Token token;
    token.kind = Tok::StringLiteral;
    token.span = span_from(start);
    token.text = interner_.intern(value);
    if (!closed) {
        diagnostics_.error(Code::UnterminatedString, "unterminated string literal")
            .label(token.span)
            .with_help("string literals cannot span lines; use \"\"\" for multi-line text");
    }
    output_.push_back(token);
}

void Lexer::lex_punctuation(u32 start) {
    const char c = peek();
    const char next = peek(1);

    // Two-character operators are matched before single characters.
    Tok kind = Tok::End;
    bool matched_pair = true;
    switch (c) {
        case '-': kind = (next == '>') ? Tok::Arrow : (next == '=') ? Tok::MinusEqual : Tok::End; break;
        case '=': kind = (next == '>') ? Tok::FatArrow : (next == '=') ? Tok::EqualEqual : Tok::End; break;
        case '<':
            kind = (next == '-') ? Tok::LeftArrow
                 : (next == '=') ? Tok::LessEqual
                 : (next == '<') ? Tok::ShiftLeft : Tok::End;
            break;
        case '>': kind = (next == '=') ? Tok::GreaterEqual : (next == '>') ? Tok::ShiftRight : Tok::End; break;
        case ':': kind = (next == ':') ? Tok::ColonColon : Tok::End; break;
        case '!': kind = (next == '=') ? Tok::BangEqual : Tok::End; break;
        case '&': kind = (next == '&') ? Tok::AmpAmp : Tok::End; break;
        case '|': kind = (next == '|') ? Tok::PipePipe : Tok::End; break;
        case '.': kind = (next == '.') ? Tok::DotDot : Tok::End; break;
        case '+': kind = (next == '=') ? Tok::PlusEqual : Tok::End; break;
        case '*': kind = (next == '=') ? Tok::StarEqual : Tok::End; break;
        case '/': kind = (next == '=') ? Tok::SlashEqual : Tok::End; break;
        case '%': kind = (next == '=') ? Tok::PercentEqual : Tok::End; break;
        default: matched_pair = false;
    }
    if (kind != Tok::End) {
        index_ += 2;
        push(kind, start);
        return;
    }
    (void)matched_pair;

    ++index_;
    switch (c) {
        case '(': ++nesting_; push(Tok::LeftParen, start); return;
        case '[': ++nesting_; push(Tok::LeftBracket, start); return;
        case ')':
            if (nesting_ == 0) {
                diagnostics_.error(Code::UnmatchedDelimiter, "unmatched ')'")
                    .label(span_from(start));
            } else {
                --nesting_;
            }
            push(Tok::RightParen, start);
            return;
        case ']':
            if (nesting_ == 0) {
                diagnostics_.error(Code::UnmatchedDelimiter, "unmatched ']'")
                    .label(span_from(start));
            } else {
                --nesting_;
            }
            push(Tok::RightBracket, start);
            return;
        case '{': push(Tok::LeftBrace, start); return;
        case '}': push(Tok::RightBrace, start); return;
        case ':': push(Tok::Colon, start); return;
        case ';': push(Tok::Semicolon, start); return;
        case ',': push(Tok::Comma, start); return;
        case '.': push(Tok::Dot, start); return;
        case '+': push(Tok::Plus, start); return;
        case '-': push(Tok::Minus, start); return;
        case '*': push(Tok::Star, start); return;
        case '/': push(Tok::Slash, start); return;
        case '%': push(Tok::Percent, start); return;
        case '=': push(Tok::Equal, start); return;
        case '<': push(Tok::Less, start); return;
        case '>': push(Tok::Greater, start); return;
        case '!': push(Tok::Bang, start); return;
        case '&': push(Tok::Amp, start); return;
        case '|': push(Tok::Pipe, start); return;
        case '^': push(Tok::Caret, start); return;
        case '~': push(Tok::Tilde, start); return;
        case '@': push(Tok::At, start); return;
        case '?': push(Tok::Question, start); return;
        default: break;
    }

    diagnostics_
        .error(Code::UnexpectedCharacter,
               std::string("unexpected character '") + c + "' in source")
        .label(span_from(start));
}

}  // namespace ppc
