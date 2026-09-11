#include "ppc/syntax/parser.hpp"

#include <algorithm>

namespace ppc {

// ---------------------------------------------------------------------------
// AST helpers
// ---------------------------------------------------------------------------

const char *binary_op_spelling(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::Modulo: return "%";
        case BinaryOp::Equal: return "==";
        case BinaryOp::NotEqual: return "!=";
        case BinaryOp::Less: return "<";
        case BinaryOp::LessEqual: return "<=";
        case BinaryOp::Greater: return ">";
        case BinaryOp::GreaterEqual: return ">=";
        case BinaryOp::And: return "and";
        case BinaryOp::Or: return "or";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::ShiftLeft: return "<<";
        case BinaryOp::ShiftRight: return ">>";
    }
    return "?";
}

const char *unary_op_spelling(UnaryOp op) {
    switch (op) {
        case UnaryOp::Negate: return "-";
        case UnaryOp::Not: return "not";
        case UnaryOp::BitNot: return "~";
        case UnaryOp::Deref: return "*";
        case UnaryOp::Ref: return "&";
        case UnaryOp::MutRef: return "&mut";
        case UnaryOp::RawRef: return "&raw";
        case UnaryOp::Await: return "await";
        case UnaryOp::Move: return "move";
        case UnaryOp::Drop: return "drop";
    }
    return "?";
}

bool is_comparison(BinaryOp op) {
    switch (op) {
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual:
            return true;
        default:
            return false;
    }
}

std::string TypeExpr::describe(const Interner &interner) const {
    switch (kind) {
        case Kind::Infer: return "_";
        case Kind::SelfType: return "Self";
        case Kind::Reference: return "&" + (element ? element->describe(interner) : "?");
        case Kind::MutRef: return "&mut " + (element ? element->describe(interner) : "?");
        case Kind::RawPointer: return "*" + (element ? element->describe(interner) : "?");
        case Kind::Function: {
            std::string result = "fn(";
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                if (i) result += ", ";
                result += arguments[i] ? arguments[i]->describe(interner) : "?";
            }
            result += ")";
            if (element) result += " -> " + element->describe(interner);
            return result;
        }
        case Kind::Named: {
            std::string result = interner.text(name);
            if (!arguments.empty()) {
                result += "<";
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (i) result += ", ";
                    result += arguments[i] ? arguments[i]->describe(interner) : "?";
                }
                result += ">";
            }
            return result;
        }
    }
    return "?";
}

std::string FunctionDecl::qualified_name(const Interner &interner) const {
    if (owner.valid()) return interner.text(owner) + "::" + interner.text(name);
    return interner.text(name);
}

// ---------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------

const Token &Parser::peek(std::size_t offset) const {
    const std::size_t position = std::min(index_ + offset, tokens_.size() - 1);
    return tokens_[position];
}

const Token &Parser::previous() const {
    return tokens_[index_ > 0 ? index_ - 1 : 0];
}

Token Parser::advance() {
    if (index_ < tokens_.size() - 1) return tokens_[index_++];
    return tokens_.back();
}

bool Parser::match(Tok kind) {
    if (!check(kind)) return false;
    advance();
    return true;
}

Token Parser::expect(Tok kind, const char *what) {
    if (check(kind)) return advance();
    error_at(peek(), Code::UnexpectedToken,
             std::string("expected ") + what + ", found " + token_spelling(peek().kind));
    return peek();
}

void Parser::skip_separators() {
    while (check(Tok::Semicolon)) advance();
}

void Parser::end_statement() {
    // A statement may be closed by an explicit `;`, by the newline the lexer
    // turned into one, or implicitly by the end of the enclosing block.
    if (check(Tok::Semicolon)) {
        skip_separators();
        return;
    }
    if (check(Tok::RightBrace) || check(Tok::Done) || check(Tok::Otherwise) || at_end()) return;
    error_at(peek(), Code::ExpectedStatementEnd,
             std::string("expected ';' or a newline after this statement, found ") +
                 token_spelling(peek().kind));
    synchronize();
}

Symbol Parser::expect_identifier(const char *what, bool allow_self) {
    if (allow_self && check(Tok::SelfKw)) {
        advance();
        return interner_.intern("self");
    }
    if (check(Tok::Identifier)) return advance().text;

    // A keyword in name position is far more likely to be a stale identifier
    // from an older program than a syntax error, so it gets its own code.
    const Token &token = peek();
    if (token.kind != Tok::End && token.text.valid() &&
        is_reserved_word(interner_.text(token.text))) {
        error_at(token, Code::ReservedWord,
                 "'" + interner_.text(token.text) + "' is a reserved word and cannot be used as " +
                     what,
                 "rename the item; reserved words cannot be redefined");
        advance();
        return interner_.intern("<error>");
    }
    error_at(token, Code::UnexpectedToken,
             std::string("expected ") + what + ", found " + token_spelling(token.kind));
    return interner_.intern("<error>");
}

void Parser::deprecated_syntax(const Token &token, const std::string &legacy,
                               const std::string &modern) {
    // A warning, not an error: 1.x promised that valid 0.6 source keeps
    // compiling. The point is to stop the second grammar being learned, and to
    // say what to write instead.
    diagnostics_
        .warning(Code::DeprecatedSyntax, "`" + legacy + "` belongs to the migration dialect")
        .label(token.span)
        .note("PunPun has one grammar; the migration forms are kept only so "
              "0.6 source still builds")
        .with_help("write " + modern + ", or run `pp migrate` over the file");
}

void Parser::error_at(const Token &token, Code code, const std::string &message,
                      const std::string &help) {
    // While recovering, suppress follow-on errors; they are almost always
    // artifacts of the first one.
    if (panic_mode_) return;
    panic_mode_ = true;
    Diagnostic &diagnostic = diagnostics_.error(code, message);
    diagnostic.label(token.span);
    if (!help.empty()) diagnostic.with_help(help);
}

void Parser::synchronize() {
    // Skip to something that plausibly starts a fresh statement or declaration,
    // so one bad line does not cascade into dozens of reports.
    while (!at_end()) {
        if (previous().kind == Tok::Semicolon) break;
        switch (peek().kind) {
            case Tok::Fn:
            case Tok::Struct:
            case Tok::Object:
            case Tok::Enum:
            case Tok::Contract:
            case Tok::Import:
            case Tok::Bring:
            case Tok::Let:
            case Tok::Const:
            case Tok::Return:
            case Tok::If:
            case Tok::While:
            case Tok::For:
            case Tok::Launch:
            case Tok::Craft:
            case Tok::Shape:
            case Tok::Keep:
            case Tok::Pin:
            case Tok::When:
            case Tok::Whilst:
            case Tok::Each:
            case Tok::Give:
            case Tok::Say:
            case Tok::Done:
            case Tok::RightBrace:
                return;
            default:
                advance();
        }
    }
}

Expr *Parser::make_expr(Expr::Kind kind, Span span) {
    Expr *node = arena_.make<Expr>();
    node->kind = kind;
    node->span = span;
    return node;
}

Stmt *Parser::make_stmt(Stmt::Kind kind, Span span) {
    Stmt *node = arena_.make<Stmt>();
    node->kind = kind;
    node->span = span;
    return node;
}

TypeExpr *Parser::make_infer(Span span) {
    TypeExpr *node = arena_.make<TypeExpr>();
    node->kind = TypeExpr::Kind::Infer;
    node->span = span;
    return node;
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

TypeExpr *Parser::parse_type() {
    const Span start = peek().span;

    if (match(Tok::Amp)) {
        TypeExpr *node = arena_.make<TypeExpr>();
        node->kind = match(Tok::Mut) ? TypeExpr::Kind::MutRef : TypeExpr::Kind::Reference;
        node->element = parse_type();
        node->span = start.merge(previous().span);
        return node;
    }
    if (match(Tok::Star)) {
        TypeExpr *node = arena_.make<TypeExpr>();
        node->kind = TypeExpr::Kind::RawPointer;
        node->element = parse_type();
        node->span = start.merge(previous().span);
        return node;
    }

    // fn(T, U) -> R. A result arrow is optional; without one the function
    // returns nothing, which keeps `fn(int)` usable for a callback.
    if (match(Tok::Fn)) {
        TypeExpr *node = arena_.make<TypeExpr>();
        node->kind = TypeExpr::Kind::Function;
        expect(Tok::LeftParen, "'(' after 'fn' in a function type");
        if (!check(Tok::RightParen)) {
            do {
                node->arguments.push_back(parse_type());
            } while (match(Tok::Comma));
        }
        expect(Tok::RightParen, "')' to close the parameter list");
        if (match(Tok::Arrow)) {
            node->element = parse_type();
        } else {
            TypeExpr *unit = arena_.make<TypeExpr>();
            unit->kind = TypeExpr::Kind::Named;
            unit->name = interner_.intern("void");
            unit->span = previous().span;
            node->element = unit;
        }
        node->span = start.merge(previous().span);
        return node;
    }

    TypeExpr *node = arena_.make<TypeExpr>();
    node->kind = TypeExpr::Kind::Named;

    if (check(Tok::Identifier)) {
        node->name = advance().text;
    } else if (check(Tok::SelfKw)) {
        advance();
        node->kind = TypeExpr::Kind::SelfType;
        node->span = start;
        return node;
    } else {
        error_at(peek(), Code::ExpectedType,
                 std::string("expected a type, found ") + token_spelling(peek().kind));
        node->kind = TypeExpr::Kind::Infer;
        node->span = start;
        return node;
    }

    // Generic arguments. `<` here is unambiguous because a type position never
    // contains a comparison.
    if (match(Tok::Less)) {
        do {
            skip_separators();
            if (check(Tok::Greater)) break;
            node->arguments.push_back(parse_type());
            skip_separators();
        } while (match(Tok::Comma));
        // `Map<K, Vec<V>>` closes with `>>`, which the lexer produced as one
        // shift token; split it so the inner and outer lists both close.
        if (check(Tok::ShiftRight)) {
            Token shift = tokens_[index_];
            Token replacement;
            replacement.kind = Tok::Greater;
            replacement.span = Span{shift.span.file, shift.span.start + 1, shift.span.end};
            tokens_[index_] = replacement;
        } else {
            expect(Tok::Greater, "'>' to close the generic argument list");
        }
    }
    node->span = start.merge(previous().span);
    return node;
}

std::vector<GenericParam> Parser::parse_generic_params() {
    std::vector<GenericParam> result;
    if (!match(Tok::Less)) return result;

    do {
        skip_separators();
        if (check(Tok::Greater)) break;
        GenericParam param;
        param.span = peek().span;
        param.name = expect_identifier("a type parameter name");
        // `T: Copy + Comparable<T>` — inline constraints are the canonical 0.6
        // form; `where` is reserved but not part of the grammar.
        if (match(Tok::Colon)) {
            do {
                param.constraints.push_back(parse_type());
            } while (match(Tok::Plus));
        }
        result.push_back(std::move(param));
        skip_separators();
    } while (match(Tok::Comma));

    if (check(Tok::ShiftRight)) {
        Token shift = tokens_[index_];
        Token replacement;
        replacement.kind = Tok::Greater;
        replacement.span = Span{shift.span.file, shift.span.start + 1, shift.span.end};
        tokens_[index_] = replacement;
    } else {
        expect(Tok::Greater, "'>' to close the type parameter list");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Top level
// ---------------------------------------------------------------------------

Module *Parser::parse(FileId file, const std::string &path) {
    Module *module = arena_.make<Module>();
    module->file = file;
    module->path = path;
    module_ = module;
    lambda_serial_ = 0;

    skip_separators();
    while (!at_end()) {
        const std::size_t before = index_;
        parse_declaration(*module);
        panic_mode_ = false;
        // Guarantee forward progress even if a production consumed nothing.
        if (index_ == before) advance();
        skip_separators();
    }
    return module;
}

void Parser::parse_declaration(Module &module) {
    switch (peek().kind) {
        case Tok::At:
            module.injections.push_back(parse_injection());
            return;
        case Tok::Extern:
            if (FunctionDecl *fn = parse_extern_native()) module.functions.push_back(fn);
            return;
        case Tok::Import:
        case Tok::Bring:
            module.imports.push_back(parse_import());
            return;
        case Tok::Contract:
            if (ContractDecl *contract = parse_contract()) module.contracts.push_back(contract);
            return;
        case Tok::Struct:
            advance();
            if (ShapeDecl *shape = parse_shape(false, false)) module.shapes.push_back(shape);
            return;
        case Tok::Object:
            advance();
            if (ShapeDecl *shape = parse_shape(true, false)) module.shapes.push_back(shape);
            return;
        case Tok::Sealed: {
            advance();
            const bool reference = !match(Tok::Struct);
            if (reference) match(Tok::Object);
            if (ShapeDecl *shape = parse_shape(reference, true)) module.shapes.push_back(shape);
            return;
        }
        case Tok::Enum:
            if (EnumDecl *decl = parse_enum()) module.enums.push_back(decl);
            return;
        case Tok::Shape:
            deprecated_syntax(peek(), "shape", "`struct`");
            advance();
            if (ShapeDecl *shape = parse_legacy_shape()) module.shapes.push_back(shape);
            return;
        case Tok::Launch:
            if (FunctionDecl *fn = parse_launch()) module.functions.push_back(fn);
            return;
        case Tok::Async: {
            advance();
            if (!check(Tok::Fn)) {
                error_at(peek(), Code::UnexpectedToken, "expected 'fn' after 'async'");
                synchronize();
                return;
            }
            advance();
            if (FunctionDecl *fn = parse_function(Symbol{}, false, Visibility::Public, true, false))
                module.functions.push_back(fn);
            return;
        }
        case Tok::Fn:
            advance();
            if (FunctionDecl *fn = parse_function(Symbol{}, false, Visibility::Public, false, false))
                module.functions.push_back(fn);
            return;
        case Tok::Public:
        case Tok::Private:
        case Tok::Protected: {
            const Visibility visibility = check(Tok::Public)    ? Visibility::Public
                                          : check(Tok::Private) ? Visibility::Private
                                                                : Visibility::Protected;
            advance();
            const bool is_async = match(Tok::Async);
            if (!match(Tok::Fn)) {
                error_at(peek(), Code::UnexpectedToken,
                         "expected 'fn' after a visibility modifier");
                synchronize();
                return;
            }
            if (FunctionDecl *fn = parse_function(Symbol{}, false, visibility, is_async, false))
                module.functions.push_back(fn);
            return;
        }
        case Tok::Craft:
            if (FunctionDecl *fn = parse_legacy_function()) module.functions.push_back(fn);
            return;
        default:
            error_at(peek(), Code::UnexpectedToken,
                     std::string("expected a declaration, found ") + token_spelling(peek().kind),
                     "top level items are fn, struct, object, enum, contract, import, and launch");
            synchronize();
            return;
    }
}

ImportDecl Parser::parse_import() {
    ImportDecl decl;
    decl.span = peek().span;
    advance();  // import / bring

    // Both `::` and `.` separate path segments. `bring math` with a single
    // segment is also valid.
    do {
        if (!check(Tok::Identifier)) break;
        decl.segments.push_back(advance().text);
    } while (match(Tok::ColonColon) || match(Tok::Dot));

    if (decl.segments.empty()) {
        error_at(peek(), Code::UnexpectedToken, "expected a module path after this import");
    }
    decl.span = decl.span.merge(previous().span);
    end_statement();
    return decl;
}

InjectionDecl Parser::parse_injection() {
    InjectionDecl decl;
    decl.span = peek().span;
    expect(Tok::At, "'@'");
    expect(Tok::Inject, "'inject' after '@'");
    expect(Tok::Arrow, "'->' after '@inject'");
    decl.language = expect_identifier("an injection language such as c or rust");
    expect(Tok::LeftParen, "'(' after the injection language");
    if (check(Tok::StringLiteral)) {
        decl.source = advance().text;
    } else {
        error_at(peek(), Code::UnexpectedToken,
                 "@inject source must be a string or triple-quoted string");
    }
    expect(Tok::RightParen, "')' after the injected source");
    decl.span = decl.span.merge(previous().span);
    end_statement();
    return decl;
}

FunctionDecl *Parser::parse_extern_native() {
    FunctionDecl *fn = arena_.make<FunctionDecl>();
    fn->span = peek().span;
    expect(Tok::Extern, "'extern'");
    expect(Tok::Native, "'native' after 'extern'");
    expect(Tok::Fn, "'fn' after 'extern native'");
    fn->name = expect_identifier("a native function name");
    fn->native_symbol = fn->name;
    fn->is_extern_native = true;
    parse_param_list(fn->params, fn);
    fn->result = match(Tok::Arrow) ? parse_type() : make_infer(previous().span);
    if (fn->result->kind == TypeExpr::Kind::Infer) {
        fn->result->kind = TypeExpr::Kind::Named;
        fn->result->name = interner_.intern("void");
    }
    fn->span = fn->span.merge(previous().span);
    end_statement();
    return fn;
}

void Parser::parse_param_list(std::vector<Param> &out, FunctionDecl *owner) {
    expect(Tok::LeftParen, "'(' to start the parameter list");
    bool seen_default = false;

    if (!check(Tok::RightParen)) {
        do {
            skip_separators();
            if (check(Tok::RightParen)) break;

            // An explicit receiver is written `self` or `mut self` and only in
            // first position. Methods that omit it get one inserted later.
            if (check(Tok::SelfKw) || (check(Tok::Mut) && check_next(Tok::SelfKw))) {
                const bool mutable_self = match(Tok::Mut);
                advance();  // self
                if (owner) {
                    owner->has_explicit_self = true;
                    owner->self_mutable = mutable_self;
                }
                skip_separators();
                continue;
            }

            Param param;
            param.span = peek().span;
            param.is_mutable = match(Tok::Mut);
            param.name = expect_identifier("a parameter name");

            // Modern `name: T`, migration dialect `name as T`.
            if (match(Tok::Colon) || match(Tok::As)) {
                param.type = parse_type();
            } else {
                error_at(peek(), Code::ExpectedType,
                         "parameters need a type annotation",
                         "write `name: Type`, or `name as Type` in the migration dialect");
                param.type = make_infer(param.span);
            }

            if (match(Tok::Equal)) {
                param.default_value = parse_expression();
                seen_default = true;
            } else if (seen_default) {
                error_at(peek(), Code::DefaultBeforeRequired,
                         "a parameter without a default cannot follow one with a default",
                         "give this parameter a default, or move it before the defaulted ones");
            }

            param.span = param.span.merge(previous().span);

            // Duplicates would silently shadow during call binding.
            for (const Param &existing : out) {
                if (existing.name == param.name) {
                    error_at(peek(), Code::DuplicateParameter,
                             "duplicate parameter '" + interner_.text(param.name) + "'");
                    break;
                }
            }
            out.push_back(std::move(param));
            skip_separators();
        } while (match(Tok::Comma));
    }
    skip_separators();
    expect(Tok::RightParen, "')' to close the parameter list");
}

FunctionDecl *Parser::parse_function(Symbol owner, bool is_method, Visibility visibility,
                                     bool is_async, bool is_initializer) {
    FunctionDecl *fn = arena_.make<FunctionDecl>();
    fn->span = previous().span;
    fn->owner = owner;
    fn->is_method = is_method;
    fn->is_async = is_async;
    fn->is_initializer = is_initializer;
    fn->visibility = visibility;

    if (is_initializer) {
        fn->name = interner_.intern("init");
    } else {
        fn->name = expect_identifier("a function name");
    }
    fn->generics = parse_generic_params();
    parse_param_list(fn->params, fn);

    if (match(Tok::Arrow) || match(Tok::Gives)) {
        fn->result = parse_type();
    } else {
        fn->result = arena_.make<TypeExpr>();
        fn->result->kind = TypeExpr::Kind::Named;
        fn->result->name = interner_.intern("void");
        fn->result->span = previous().span;
    }

    // `fn main()` is an entry point in the same way `launch` is.
    if (!is_method && !owner.valid() && interner_.text(fn->name) == "main") fn->is_entry = true;

    fn->body = parse_block("a function body");
    fn->span = fn->span.merge(previous().span);
    return fn;
}

FunctionDecl *Parser::parse_legacy_function() {
    deprecated_syntax(peek(), "craft", "`fn`");
    expect(Tok::Craft, "'craft'");
    FunctionDecl *fn = arena_.make<FunctionDecl>();
    fn->span = previous().span;
    fn->name = expect_identifier("a function name");
    fn->generics = parse_generic_params();
    parse_param_list(fn->params, fn);

    if (match(Tok::Gives) || match(Tok::Arrow)) {
        fn->result = parse_type();
    } else {
        fn->result = arena_.make<TypeExpr>();
        fn->result->kind = TypeExpr::Kind::Named;
        fn->result->name = interner_.intern("void");
        fn->result->span = previous().span;
    }
    if (interner_.text(fn->name) == "main") fn->is_entry = true;
    fn->body = parse_block("a function body");
    fn->span = fn->span.merge(previous().span);
    return fn;
}

FunctionDecl *Parser::parse_launch() {
    expect(Tok::Launch, "'launch'");
    FunctionDecl *fn = arena_.make<FunctionDecl>();
    fn->span = previous().span;
    // A launch block is the program entry point; it lowers to the same shape as
    // `fn main()` so nothing downstream needs a special case.
    fn->name = interner_.intern("main");
    fn->is_entry = true;
    fn->result = arena_.make<TypeExpr>();
    fn->result->kind = TypeExpr::Kind::Named;
    fn->result->name = interner_.intern("void");
    fn->result->span = fn->span;
    fn->body = parse_block("a launch block");
    fn->span = fn->span.merge(previous().span);
    return fn;
}

// ---------------------------------------------------------------------------
// Aggregates
// ---------------------------------------------------------------------------

ShapeDecl *Parser::parse_shape(bool is_reference, bool is_sealed) {
    ShapeDecl *shape = arena_.make<ShapeDecl>();
    shape->span = previous().span;
    shape->is_reference = is_reference;
    shape->is_sealed = is_sealed;
    shape->name = expect_identifier("a type name");
    shape->generics = parse_generic_params();

    // `object Enemy meets Damageable, Named { ... }`
    if (match(Tok::Meets)) {
        do {
            shape->contracts.push_back(expect_identifier("a contract name"));
        } while (match(Tok::Comma));
    }

    parse_shape_body(*shape);
    shape->span = shape->span.merge(previous().span);
    return shape;
}

void Parser::parse_shape_body(ShapeDecl &shape) {
    const BlockStyle style = open_block("a type body");
    skip_separators();

    while (!at_block_end(style) && !at_end()) {
        const std::size_t before = index_;
        Visibility visibility = Visibility::Public;
        if (match(Tok::Public)) visibility = Visibility::Public;
        else if (match(Tok::Private)) visibility = Visibility::Private;
        else if (match(Tok::Protected)) visibility = Visibility::Protected;

        const bool is_async = match(Tok::Async);

        if (match(Tok::Init)) {
            FunctionDecl *init =
                parse_function(shape.name, true, visibility, is_async, true);
            shape.initializer = init;
            shape.methods.push_back(init);
            skip_separators();
            continue;
        }
        if (match(Tok::Fn)) {
            FunctionDecl *method =
                parse_function(shape.name, true, visibility, is_async, false);
            shape.methods.push_back(method);
            skip_separators();
            continue;
        }

        // Otherwise this is a field. `let` and `let mut` are optional noise that
        // some sources include for symmetry with local bindings.
        Param field;
        field.span = peek().span;
        field.visibility = visibility;
        if (match(Tok::Let)) field.is_mutable = match(Tok::Mut);
        field.name = expect_identifier("a field name");
        if (match(Tok::Colon) || match(Tok::As)) {
            field.type = parse_type();
        } else {
            error_at(peek(), Code::ExpectedType, "fields need a type annotation",
                     "write `name: Type`, or `name as Type` in the migration dialect");
            field.type = make_infer(field.span);
        }
        if (match(Tok::Equal)) field.default_value = parse_expression();
        field.span = field.span.merge(previous().span);
        shape.fields.push_back(std::move(field));

        // Fields may be separated by `,`, `;`, or a newline.
        if (!match(Tok::Comma)) skip_separators();
        skip_separators();
        // A missing type-body opener can leave a declaration-start token where
        // a field was expected. Error recovery must always consume something.
        if (index_ == before) advance();
    }
    close_block(style, "a type body");
}

ShapeDecl *Parser::parse_legacy_shape() {
    ShapeDecl *shape = arena_.make<ShapeDecl>();
    shape->span = previous().span;
    shape->is_reference = false;  // `shape` has value semantics, like `struct`
    shape->name = expect_identifier("a type name");
    shape->generics = parse_generic_params();
    parse_shape_body(*shape);
    shape->span = shape->span.merge(previous().span);
    return shape;
}

EnumDecl *Parser::parse_enum() {
    expect(Tok::Enum, "'enum'");
    EnumDecl *decl = arena_.make<EnumDecl>();
    decl->span = previous().span;
    decl->name = expect_identifier("an enum name");
    decl->generics = parse_generic_params();

    const BlockStyle style = open_block("an enum body");
    skip_separators();

    while (!at_block_end(style) && !at_end()) {
        const std::size_t before = index_;
        EnumVariantDecl variant;
        variant.span = peek().span;
        variant.name = expect_identifier("a variant name");
        if (match(Tok::LeftParen)) {
            if (!check(Tok::RightParen)) {
                do {
                    skip_separators();
                    if (check(Tok::RightParen)) break;
                    variant.payload.push_back(parse_type());
                    skip_separators();
                } while (match(Tok::Comma));
            }
            expect(Tok::RightParen, "')' to close the variant payload");
        }
        variant.span = variant.span.merge(previous().span);

        for (const EnumVariantDecl &existing : decl->variants) {
            if (existing.name == variant.name) {
                error_at(peek(), Code::DuplicateDefinition,
                         "duplicate variant '" + interner_.text(variant.name) + "'");
                break;
            }
        }
        decl->variants.push_back(std::move(variant));

        if (!match(Tok::Comma)) skip_separators();
        skip_separators();
        if (index_ == before) advance();
    }
    close_block(style, "an enum body");
    decl->span = decl->span.merge(previous().span);
    return decl;
}

ContractDecl *Parser::parse_contract() {
    expect(Tok::Contract, "'contract'");
    ContractDecl *decl = arena_.make<ContractDecl>();
    decl->span = previous().span;
    decl->name = expect_identifier("a contract name");
    decl->generics = parse_generic_params();

    const BlockStyle style = open_block("a contract body");
    skip_separators();

    while (!at_block_end(style) && !at_end()) {
        match(Tok::Public);
        if (!match(Tok::Fn)) {
            error_at(peek(), Code::UnexpectedToken,
                     "a contract body contains only method signatures");
            synchronize();
            break;
        }
        ContractMethodDecl method;
        method.span = previous().span;
        method.name = expect_identifier("a method name");
        parse_param_list(method.params, nullptr);
        method.result = match(Tok::Arrow) ? parse_type() : nullptr;
        if (!method.result) {
            method.result = arena_.make<TypeExpr>();
            method.result->kind = TypeExpr::Kind::Named;
            method.result->name = interner_.intern("void");
            method.result->span = method.span;
        }
        method.span = method.span.merge(previous().span);
        decl->methods.push_back(std::move(method));
        end_statement();
        skip_separators();
    }
    close_block(style, "a contract body");
    decl->span = decl->span.merge(previous().span);
    return decl;
}

}  // namespace ppc
