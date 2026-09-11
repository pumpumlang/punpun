#include "ppc/syntax/parser.hpp"

namespace ppc {

// ---------------------------------------------------------------------------
// Blocks
//
// The two dialects differ only in how a block is delimited: `{ ... }` in the
// modern grammar, `: ... done` in the migration dialect. Opening a block records
// which style was used so the matching close accepts exactly one terminator.
// ---------------------------------------------------------------------------

Parser::BlockStyle Parser::open_block(const char *what) {
    if (match(Tok::LeftBrace)) return BlockStyle::Brace;
    if (match(Tok::Colon)) return BlockStyle::Legacy;
    error_at(peek(), Code::UnexpectedToken,
             std::string("expected '{' or ':' to start ") + what + ", found " +
                 token_spelling(peek().kind));
    // Assume the modern form so the caller can keep parsing statements.
    return BlockStyle::Brace;
}

bool Parser::at_block_end(BlockStyle style) const {
    if (style == BlockStyle::Brace) return check(Tok::RightBrace);
    // `otherwise` ends the consequent of a legacy `when` without closing it.
    return check(Tok::Done) || check(Tok::Otherwise);
}

void Parser::close_block(BlockStyle style, const char *what) {
    if (style == BlockStyle::Brace) {
        if (!match(Tok::RightBrace)) {
            error_at(peek(), Code::ExpectedBlockEnd,
                     std::string("expected '}' to close ") + what);
        }
        return;
    }
    if (!match(Tok::Done)) {
        error_at(peek(), Code::ExpectedBlockEnd,
                 std::string("expected 'done' to close ") + what);
    }
}

std::vector<Stmt *> Parser::parse_block_body(BlockStyle style) {
    std::vector<Stmt *> body;
    skip_separators();
    while (!at_block_end(style) && !at_end()) {
        const std::size_t before = index_;
        if (Stmt *statement = parse_statement()) body.push_back(statement);
        panic_mode_ = false;
        if (index_ == before) advance();  // guarantee progress after an error
        skip_separators();
    }
    return body;
}

std::vector<Stmt *> Parser::parse_block(const char *what) {
    const BlockStyle style = open_block(what);
    std::vector<Stmt *> body = parse_block_body(style);
    close_block(style, what);
    return body;
}

bool Parser::match_across_separators(Tok kind) {
    std::size_t lookahead = index_;
    while (lookahead < tokens_.size() && tokens_[lookahead].kind == Tok::Semicolon) ++lookahead;
    if (lookahead >= tokens_.size() || tokens_[lookahead].kind != kind) return false;
    index_ = lookahead + 1;
    return true;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

Stmt *Parser::parse_statement() {
    switch (peek().kind) {
        case Tok::Let: {
            advance();
            const bool is_mutable = match(Tok::Mut);
            return parse_let(is_mutable, false, false);
        }
        case Tok::Const:
            advance();
            return parse_let(false, true, false);
        // `keep` is a mutable binding; `pin` is immutable.
        case Tok::Keep:
            advance();
            return parse_let(true, false, true);
        case Tok::Pin:
            advance();
            return parse_let(false, false, true);
        case Tok::If:
        case Tok::When:
            return parse_if();
        case Tok::While:
        case Tok::Whilst:
            return parse_while();
        case Tok::For:
            return parse_for();
        case Tok::Each:
            return parse_each();
        case Tok::Return:
            return parse_return(false);
        case Tok::Give:
            return parse_return(true);
        case Tok::Say:
            return parse_say();
        case Tok::Unsafe:
            return parse_unsafe();
        case Tok::Break:
        case Tok::Leave: {
            Stmt *statement = make_stmt(Stmt::Kind::Break, peek().span);
            advance();
            if (loop_depth_ == 0) {
                error_at(previous(), Code::UnexpectedToken,
                         "'" + std::string(token_spelling(previous().kind)) +
                             "' is only valid inside a loop");
            }
            end_statement();
            return statement;
        }
        case Tok::Continue:
        case Tok::Next: {
            Stmt *statement = make_stmt(Stmt::Kind::Continue, peek().span);
            advance();
            if (loop_depth_ == 0) {
                error_at(previous(), Code::UnexpectedToken,
                         "'" + std::string(token_spelling(previous().kind)) +
                             "' is only valid inside a loop");
            }
            end_statement();
            return statement;
        }
        case Tok::LeftBrace: {
            Stmt *statement = make_stmt(Stmt::Kind::Block, peek().span);
            statement->body = parse_block("a block");
            statement->span = statement->span.merge(previous().span);
            return statement;
        }
        default:
            return parse_expression_statement();
    }
}

Stmt *Parser::parse_let(bool is_mutable, bool is_const, bool legacy) {
    Stmt *statement = make_stmt(Stmt::Kind::Let, previous().span);
    statement->is_mutable = is_mutable;
    statement->is_const = is_const;
    statement->name = expect_identifier("a binding name");

    if (match(Tok::Colon) || match(Tok::As)) {
        statement->declared_type = parse_type();
    } else {
        statement->declared_type = make_infer(statement->span);
    }

    // `=` in the modern grammar, `<-` in the migration dialect. Both are
    // accepted everywhere so a partially migrated file still parses.
    if (match(Tok::Equal) || match(Tok::LeftArrow)) {
        statement->value = parse_expression();
    } else if (!legacy && is_const) {
        error_at(peek(), Code::UnexpectedToken, "a const binding needs an initializer");
    } else if (!statement->declared_type ||
               statement->declared_type->kind == TypeExpr::Kind::Infer) {
        error_at(peek(), Code::CannotInfer,
                 "a binding without an initializer needs a type annotation",
                 "write `let name: Type` or give it a value");
    }

    statement->span = statement->span.merge(previous().span);
    end_statement();
    return statement;
}

Stmt *Parser::parse_if() {
    Stmt *statement = make_stmt(Stmt::Kind::If, peek().span);
    advance();  // `if` or `when`

    statement->value = parse_expression();
    const BlockStyle style = open_block("the conditional body");
    statement->body = parse_block_body(style);

    // In the migration dialect `otherwise` terminates the consequent instead of
    // closing it: the whole if/else shares a single trailing `done`. In the
    // modern grammar the consequent closes with `}` and `else` comes after.
    // Both spellings are accepted in either style so a half-migrated file works.
    if (style == BlockStyle::Legacy && (check(Tok::Otherwise) || check(Tok::Else))) {
        advance();
        if (check(Tok::When) || check(Tok::If)) {
            // `otherwise when cond:` — the nested if consumes the final `done`.
            statement->alternative.push_back(parse_if());
        } else {
            const BlockStyle alternate = open_block("an 'otherwise' block");
            statement->alternative = parse_block_body(alternate);
            close_block(alternate, "an 'otherwise' block");
        }
    } else {
        close_block(style, "the conditional body");
        if (match_across_separators(Tok::Else) || match_across_separators(Tok::Otherwise)) {
            if (check(Tok::If) || check(Tok::When)) {
                statement->alternative.push_back(parse_if());
            } else {
                statement->alternative = parse_block("an 'else' block");
            }
        }
    }

    statement->span = statement->span.merge(previous().span);
    return statement;
}

Stmt *Parser::parse_while() {
    Stmt *statement = make_stmt(Stmt::Kind::While, peek().span);
    advance();
    statement->value = parse_expression();
    ++loop_depth_;
    statement->body = parse_block("a loop body");
    --loop_depth_;
    statement->span = statement->span.merge(previous().span);
    return statement;
}

Stmt *Parser::parse_for() {
    Stmt *statement = make_stmt(Stmt::Kind::For, peek().span);
    expect(Tok::For, "'for'");
    statement->name = expect_identifier("a loop variable name");
    expect(Tok::In, "'in' after the loop variable");

    // `for i in 0..n` — the range is parsed as an ordinary expression so the
    // bounds can be arbitrary, then split into its two halves here.
    Expr *iterable = parse_expression();
    if (iterable && iterable->kind == Expr::Kind::Range) {
        statement->range_start = iterable->left;
        statement->range_end = iterable->right;
    } else {
        // A sequence. The checker resolves the element type and rewrites this
        // into an indexed loop, so no later stage sees a second loop form.
        statement->iterable = iterable;
    }

    ++loop_depth_;
    statement->body = parse_block("a loop body");
    --loop_depth_;
    statement->span = statement->span.merge(previous().span);
    return statement;
}

Stmt *Parser::parse_each() {
    Stmt *statement = make_stmt(Stmt::Kind::For, peek().span);
    expect(Tok::Each, "'each'");
    statement->name = expect_identifier("a loop variable name");
    expect(Tok::From, "'from' after the loop variable");
    statement->range_start = parse_expression();
    expect(Tok::Until, "'until' after the range start");
    statement->range_end = parse_expression();

    ++loop_depth_;
    statement->body = parse_block("a loop body");
    --loop_depth_;
    statement->span = statement->span.merge(previous().span);
    return statement;
}

Stmt *Parser::parse_return(bool legacy) {
    Stmt *statement = make_stmt(Stmt::Kind::Return, peek().span);
    advance();
    // A bare `return` is legal in a void function; anything that could start an
    // expression is treated as the returned value.
    const bool has_value = !check(Tok::Semicolon) && !check(Tok::RightBrace) &&
                           !check(Tok::Done) && !check(Tok::Otherwise) && !at_end();
    if (has_value) statement->value = parse_expression();
    (void)legacy;
    statement->span = statement->span.merge(previous().span);
    end_statement();
    return statement;
}

Stmt *Parser::parse_say() {
    // `say expr` is the migration dialect's print statement. It lowers to a call
    // to the `say` builtin so only one form reaches the checker.
    Stmt *statement = make_stmt(Stmt::Kind::Expression, peek().span);
    const Span keyword = peek().span;
    advance();

    Expr *call = make_expr(Expr::Kind::Call, keyword);
    Expr *callee = make_expr(Expr::Kind::Name, keyword);
    callee->name = interner_.intern("say");
    call->left = callee;

    // `say(x)` already looks like a call; `say x` does not. Both end up here.
    Argument argument;
    argument.span = peek().span;
    argument.value = parse_expression();
    argument.span = argument.span.merge(previous().span);

    // Unwrap `say(x)` so the argument is not double-wrapped in a call.
    if (argument.value && argument.value->kind == Expr::Kind::Call &&
        argument.value->left && argument.value->left->kind == Expr::Kind::Name &&
        interner_.text(argument.value->left->name) == "say") {
        statement->value = argument.value;
        statement->span = statement->span.merge(previous().span);
        end_statement();
        return statement;
    }

    call->arguments.push_back(argument);
    call->span = keyword.merge(previous().span);
    statement->value = call;
    statement->span = statement->span.merge(previous().span);
    end_statement();
    return statement;
}

Stmt *Parser::parse_unsafe() {
    Stmt *statement = make_stmt(Stmt::Kind::Unsafe, peek().span);
    advance();
    statement->body = parse_block("an unsafe block");
    statement->span = statement->span.merge(previous().span);
    return statement;
}

Stmt *Parser::parse_expression_statement() {
    const Span start = peek().span;
    Expr *expression = parse_expression();

    // Assignment is a statement, not an expression, so `a = b` inside a
    // condition is a parse error rather than a silent truthiness bug.
    BinaryOp compound = BinaryOp::Add;
    bool is_compound = false;
    bool is_assignment = false;

    switch (peek().kind) {
        case Tok::Equal:
        case Tok::LeftArrow: is_assignment = true; break;
        case Tok::PlusEqual: is_assignment = is_compound = true; compound = BinaryOp::Add; break;
        case Tok::MinusEqual: is_assignment = is_compound = true; compound = BinaryOp::Subtract; break;
        case Tok::StarEqual: is_assignment = is_compound = true; compound = BinaryOp::Multiply; break;
        case Tok::SlashEqual: is_assignment = is_compound = true; compound = BinaryOp::Divide; break;
        case Tok::PercentEqual: is_assignment = is_compound = true; compound = BinaryOp::Modulo; break;
        default: break;
    }

    if (is_assignment) {
        Stmt *statement = make_stmt(Stmt::Kind::Assign, start);
        advance();
        statement->target = expression;
        statement->is_compound = is_compound;
        statement->compound_op = compound;
        statement->value = parse_expression();
        statement->span = start.merge(previous().span);
        end_statement();
        return statement;
    }

    Stmt *statement = make_stmt(Stmt::Kind::Expression, start);
    statement->value = expression;
    statement->span = start.merge(previous().span);
    end_statement();
    return statement;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

namespace {

/// Binding powers, lowest first. These mirror the reference implementation so a
/// program compiled by either front end evaluates identically.
int precedence_of(Tok kind) {
    switch (kind) {
        case Tok::Or:
        case Tok::PipePipe: return 1;
        case Tok::And:
        case Tok::AmpAmp: return 2;
        case Tok::Pipe: return 3;
        case Tok::Caret: return 4;
        case Tok::Amp: return 5;
        case Tok::EqualEqual:
        case Tok::BangEqual: return 6;
        case Tok::Less:
        case Tok::LessEqual:
        case Tok::Greater:
        case Tok::GreaterEqual: return 7;
        case Tok::ShiftLeft:
        case Tok::ShiftRight: return 8;
        case Tok::Plus:
        case Tok::Minus: return 9;
        case Tok::Star:
        case Tok::Slash:
        case Tok::Percent: return 10;
        default: return 0;
    }
}

BinaryOp binary_op_of(Tok kind) {
    switch (kind) {
        case Tok::Or:
        case Tok::PipePipe: return BinaryOp::Or;
        case Tok::And:
        case Tok::AmpAmp: return BinaryOp::And;
        case Tok::Pipe: return BinaryOp::BitOr;
        case Tok::Caret: return BinaryOp::BitXor;
        case Tok::Amp: return BinaryOp::BitAnd;
        case Tok::EqualEqual: return BinaryOp::Equal;
        case Tok::BangEqual: return BinaryOp::NotEqual;
        case Tok::Less: return BinaryOp::Less;
        case Tok::LessEqual: return BinaryOp::LessEqual;
        case Tok::Greater: return BinaryOp::Greater;
        case Tok::GreaterEqual: return BinaryOp::GreaterEqual;
        case Tok::ShiftLeft: return BinaryOp::ShiftLeft;
        case Tok::ShiftRight: return BinaryOp::ShiftRight;
        case Tok::Plus: return BinaryOp::Add;
        case Tok::Minus: return BinaryOp::Subtract;
        case Tok::Star: return BinaryOp::Multiply;
        case Tok::Slash: return BinaryOp::Divide;
        case Tok::Percent: return BinaryOp::Modulo;
        default: return BinaryOp::Add;
    }
}

}  // namespace

Expr *Parser::parse_expression() {
    Expr *left = parse_binary(1);

    // `..` sits below every binary operator and only appears in `for` headers,
    // so it is handled once here rather than in the precedence climb.
    if (check(Tok::DotDot)) {
        const Span span = peek().span;
        advance();
        Expr *range = make_expr(Expr::Kind::Range, span);
        range->left = left;
        range->right = parse_binary(1);
        range->span = left->span.merge(previous().span);
        return range;
    }
    return left;
}

Expr *Parser::parse_binary(int min_precedence) {
    Expr *left = parse_unary();

    while (true) {
        const int precedence = precedence_of(peek().kind);
        if (precedence == 0 || precedence < min_precedence) break;

        const Token op = advance();
        // Left-associative: the right operand binds tighter by one level.
        Expr *right = parse_binary(precedence + 1);

        Expr *node = make_expr(Expr::Kind::Binary, op.span);
        node->binary_op = binary_op_of(op.kind);
        node->left = left;
        node->right = right;
        node->span = left->span.merge(right ? right->span : op.span);
        left = node;
    }
    return left;
}

Expr *Parser::parse_unary() {
    const Token &token = peek();
    UnaryOp op;
    bool is_unary = true;

    switch (token.kind) {
        case Tok::Minus: op = UnaryOp::Negate; break;
        case Tok::Not:
        case Tok::Bang: op = UnaryOp::Not; break;
        case Tok::Tilde: op = UnaryOp::BitNot; break;
        case Tok::Star: op = UnaryOp::Deref; break;
        case Tok::Await: op = UnaryOp::Await; break;
        case Tok::Amp: op = UnaryOp::Ref; break;
        default: is_unary = false; op = UnaryOp::Negate; break;
    }

    if (is_unary) {
        const Span span = advance().span;
        // `&mut x` and `&raw x` are distinct operators sharing a leading `&`.
        if (op == UnaryOp::Ref) {
            if (match(Tok::Mut)) op = UnaryOp::MutRef;
            else if (match(Tok::Raw)) op = UnaryOp::RawRef;
        }
        Expr *node = make_expr(Expr::Kind::Unary, span);
        node->unary_op = op;
        node->left = parse_unary();
        node->span = span.merge(node->left ? node->left->span : span);
        return node;
    }

    return parse_postfix(parse_primary());
}

Expr *Parser::parse_postfix(Expr *base) {
    while (true) {
        if (check(Tok::Dot)) {
            const Span dot = advance().span;
            const Symbol member = expect_identifier("a field or method name");
            if (match(Tok::LeftParen)) {
                Expr *call = make_expr(Expr::Kind::MethodCall, dot);
                call->name = member;
                call->left = base;
                parse_arguments(*call);
                call->span = base->span.merge(previous().span);
                base = call;
            } else {
                Expr *field = make_expr(Expr::Kind::Field, dot);
                field->name = member;
                field->left = base;
                field->span = base->span.merge(previous().span);
                base = field;
            }
            continue;
        }
        if (check(Tok::LeftBracket)) {
            const Span bracket = advance().span;
            Expr *index = make_expr(Expr::Kind::Index, bracket);
            index->left = base;
            index->right = parse_expression();
            expect(Tok::RightBracket, "']' after the index");
            index->span = base->span.merge(previous().span);
            base = index;
            continue;
        }
        if (check(Tok::LeftParen)) {
            advance();
            Expr *call = make_expr(Expr::Kind::Call, base->span);
            call->left = base;
            parse_arguments(*call);
            call->span = base->span.merge(previous().span);
            base = call;
            continue;
        }
        if (check(Tok::Question)) {
            const Span span = advance().span;
            Expr *propagate = make_expr(Expr::Kind::Propagate, span);
            propagate->left = base;
            propagate->span = base->span.merge(span);
            base = propagate;
            continue;
        }
        break;
    }
    return base;
}

void Parser::parse_arguments(Expr &call) {
    // The '(' is already consumed by the caller.
    bool seen_named = false;
    if (!check(Tok::RightParen)) {
        do {
            skip_separators();
            if (check(Tok::RightParen)) break;

            Argument argument;
            argument.span = peek().span;

            // `name: value` is a named argument. Lookahead is needed because a
            // bare identifier is also a valid positional expression.
            if (check(Tok::Identifier) && check_next(Tok::Colon)) {
                argument.name = advance().text;
                advance();  // ':'
                seen_named = true;
            } else if (seen_named) {
                error_at(peek(), Code::PositionalAfterNamed,
                         "positional arguments must come before named arguments");
            }

            argument.value = parse_expression();
            argument.span = argument.span.merge(previous().span);

            for (const Argument &existing : call.arguments) {
                if (existing.name.valid() && existing.name == argument.name) {
                    error_at(peek(), Code::DuplicateArgument,
                             "argument '" + interner_.text(argument.name) + "' is given twice");
                    break;
                }
            }
            call.arguments.push_back(argument);
            skip_separators();
        } while (match(Tok::Comma));
    }
    skip_separators();
    expect(Tok::RightParen, "')' to close the argument list");
}

Expr *Parser::parse_list_literal() {
    const Span start = previous().span;
    Expr *list = make_expr(Expr::Kind::ListLiteral, start);
    if (!check(Tok::RightBracket)) {
        do {
            skip_separators();
            if (check(Tok::RightBracket)) break;
            list->elements.push_back(parse_expression());
            skip_separators();
        } while (match(Tok::Comma));
    }
    expect(Tok::RightBracket, "']' to close the list literal");
    list->span = start.merge(previous().span);
    return list;
}

Expr *Parser::parse_match() {
    const Span start = previous().span;
    Expr *node = make_expr(Expr::Kind::Match, start);
    node->left = parse_expression();

    const BlockStyle style = open_block("the match arms");
    skip_separators();

    while (!at_block_end(style) && !at_end()) {
        MatchArm arm;
        arm.span = peek().span;
        arm.pattern = parse_pattern();

        // Guards parse but never count toward exhaustiveness; the checker
        // enforces that rule.
        if (match(Tok::If)) arm.guard = parse_expression();

        expect(Tok::FatArrow, "'=>' after the pattern");

        if (check(Tok::LeftBrace)) {
            arm.has_block = true;
            arm.body = parse_block("a match arm body");
        } else {
            arm.value = parse_expression();
        }
        arm.span = arm.span.merge(previous().span);
        node->arms.push_back(std::move(arm));

        if (!match(Tok::Comma)) skip_separators();
        skip_separators();
    }
    close_block(style, "the match arms");
    node->span = start.merge(previous().span);
    return node;
}

/// `fn(parameters) -> result { body }` written in expression position.
///
/// The literal is lifted to a module-level function with a generated name, and
/// the expression becomes a reference to it. Everything downstream then treats
/// it exactly like a named function used as a value, so no later stage needs a
/// second notion of what a function is.
///
/// A lifted function has no enclosing scope, so naming a local from the
/// surrounding function does not resolve. That is deliberate for now:
/// capturing closures need an environment to own the captured values, and the
/// ownership rules have to say what that means before the syntax exists.
Expr *Parser::parse_lambda() {
    const Span start = peek().span;
    expect(Tok::Fn, "'fn'");

    FunctionDecl *fn = arena_.make<FunctionDecl>();
    fn->span = start;
    fn->name = interner_.intern("__pp_lambda_" + std::to_string(lambda_serial_++));
    fn->is_lambda = true;
    parse_param_list(fn->params, fn);

    if (match(Tok::Arrow) || match(Tok::Gives)) {
        fn->result = parse_type();
    } else {
        fn->result = arena_.make<TypeExpr>();
        fn->result->kind = TypeExpr::Kind::Named;
        fn->result->name = interner_.intern("void");
        fn->result->span = previous().span;
    }
    fn->body = parse_block("a function body");
    fn->span = start.merge(previous().span);
    if (module_) module_->functions.push_back(fn);

    Expr *reference = make_expr(Expr::Kind::Name, fn->span);
    reference->name = fn->name;
    return reference;
}

Expr *Parser::parse_primary() {
    const Token token = peek();

    switch (token.kind) {
        case Tok::Fn: return parse_lambda();
        case Tok::IntLiteral: {
            advance();
            Expr *node = make_expr(Expr::Kind::IntLiteral, token.span);
            node->int_value = token.int_value;
            return node;
        }
        case Tok::FloatLiteral: {
            advance();
            Expr *node = make_expr(Expr::Kind::FloatLiteral, token.span);
            node->float_value = token.float_value;
            return node;
        }
        case Tok::StringLiteral: {
            advance();
            Expr *node = make_expr(Expr::Kind::StringLiteral, token.span);
            node->string_value = token.text;
            return node;
        }
        case Tok::True:
        case Tok::False: {
            advance();
            Expr *node = make_expr(Expr::Kind::BoolLiteral, token.span);
            node->bool_value = (token.kind == Tok::True);
            return node;
        }
        case Tok::SelfKw: {
            advance();
            return make_expr(Expr::Kind::SelfExpr, token.span);
        }
        case Tok::LeftBracket:
            advance();
            return parse_list_literal();
        case Tok::LeftParen: {
            advance();
            Expr *inner = parse_expression();
            expect(Tok::RightParen, "')' to close the parenthesized expression");
            return inner;
        }
        case Tok::Match:
            advance();
            return parse_match();
        case Tok::Move:
        case Tok::Unsafe: {
            // `move(x)` and `drop(x)` look like calls and are parsed as such;
            // only `move` is a keyword, so it is redirected into a Name here.
            advance();
            Expr *node = make_expr(Expr::Kind::Name, token.span);
            node->name = interner_.intern(token.kind == Tok::Move ? "move" : "unsafe");
            return node;
        }
        case Tok::SizeOf:
        case Tok::AlignOf: {
            advance();
            Expr *node = make_expr(token.kind == Tok::SizeOf ? Expr::Kind::SizeOf
                                                             : Expr::Kind::AlignOf,
                                   token.span);
            expect(Tok::LeftParen, "'(' after the operator");
            node->type_operand = parse_type();
            expect(Tok::RightParen, "')' after the type");
            node->span = token.span.merge(previous().span);
            return node;
        }
        case Tok::Identifier: {
            advance();
            // `Enum::Variant` — the qualifier is kept so the checker can look the
            // variant up in the enum's namespace rather than the module's.
            if (check(Tok::ColonColon)) {
                advance();
                Expr *node = make_expr(Expr::Kind::Path, token.span);
                node->qualifier = token.text;
                node->name = expect_identifier("a variant or member name");
                if (check(Tok::Less)) {
                    // `<` here could open a type argument list or be a
                    // comparison. Parse speculatively and discard both the
                    // tokens and any diagnostics if the guess was wrong.
                    const std::size_t save = index_;
                    const std::size_t reported = diagnostics_.mark();
                    advance();
                    std::vector<TypeExpr *> arguments;
                    bool ok = true;
                    do {
                        if (check(Tok::Greater)) break;
                        arguments.push_back(parse_type());
                    } while (match(Tok::Comma));
                    if (ok && match(Tok::Greater)) {
                        node->type_arguments = std::move(arguments);
                    } else {
                        ok = false;
                    }
                    if (!ok) {
                        index_ = save;
                        diagnostics_.rewind(reported);
                        panic_mode_ = false;
                    }
                }
                node->span = token.span.merge(previous().span);
                return node;
            }

            Expr *node = make_expr(Expr::Kind::Name, token.span);
            node->name = token.text;

            // `identity<int>(x)` spells its type arguments. A bare `a < b` is a
            // comparison, so this only commits when a matching `>` is followed
            // by `(`; otherwise the tokens are rewound and reparsed as `<`.
            if (check(Tok::Less)) {
                // `a < b` is a comparison and `f<int>(x)` is a generic call.
                // They are only distinguishable by looking for a matching `>`
                // followed by `(`, so this parse is speculative: on failure the
                // token position and any diagnostics it produced are both
                // rolled back, and the `<` is reparsed as a comparison.
                const std::size_t save = index_;
                const std::size_t reported = diagnostics_.mark();
                advance();
                std::vector<TypeExpr *> arguments;
                bool ok = true;
                if (!check(Tok::Greater)) {
                    do {
                        if (check(Tok::Greater)) break;
                        arguments.push_back(parse_type());
                    } while (match(Tok::Comma));
                }
                if (ok && match(Tok::Greater) && check(Tok::LeftParen)) {
                    node->type_arguments = std::move(arguments);
                } else {
                    ok = false;
                }
                if (!ok) {
                    index_ = save;
                    diagnostics_.rewind(reported);
                    panic_mode_ = false;
                }
            }
            return node;
        }
        default:
            break;
    }

    error_at(token, Code::UnexpectedToken,
             std::string("expected an expression, found ") + token_spelling(token.kind));
    Expr *node = make_expr(Expr::Kind::IntLiteral, token.span);
    node->int_value = 0;
    if (!at_end()) advance();
    return node;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

Pattern *Parser::parse_pattern() {
    Pattern *pattern = arena_.make<Pattern>();
    pattern->span = peek().span;

    switch (peek().kind) {
        case Tok::IntLiteral: {
            const Token token = advance();
            pattern->kind = Pattern::Kind::Int;
            pattern->int_value = token.int_value;
            pattern->span = token.span;
            return pattern;
        }
        case Tok::Minus: {
            // Negative integer patterns.
            const Span span = advance().span;
            const Token token = expect(Tok::IntLiteral, "an integer literal after '-'");
            pattern->kind = Pattern::Kind::Int;
            pattern->int_value = -token.int_value;
            pattern->span = span.merge(token.span);
            return pattern;
        }
        case Tok::StringLiteral: {
            const Token token = advance();
            pattern->kind = Pattern::Kind::String;
            pattern->string_value = token.text;
            pattern->span = token.span;
            return pattern;
        }
        case Tok::True:
        case Tok::False: {
            const Token token = advance();
            pattern->kind = Pattern::Kind::Bool;
            pattern->bool_value = (token.kind == Tok::True);
            pattern->span = token.span;
            return pattern;
        }
        case Tok::Identifier: {
            const Token token = advance();
            // `_` is the wildcard; every other bare name is a fresh binding,
            // because variants live in their enum's namespace and must be
            // written qualified.
            if (interner_.text(token.text) == "_") {
                pattern->kind = Pattern::Kind::Wildcard;
                pattern->span = token.span;
                return pattern;
            }
            if (check(Tok::ColonColon)) {
                advance();
                pattern->kind = Pattern::Kind::Variant;
                pattern->enum_name = token.text;
                pattern->name = expect_identifier("a variant name");
                if (match(Tok::LeftParen)) {
                    if (!check(Tok::RightParen)) {
                        do {
                            skip_separators();
                            if (check(Tok::RightParen)) break;
                            pattern->children.push_back(parse_pattern());
                            skip_separators();
                        } while (match(Tok::Comma));
                    }
                    expect(Tok::RightParen, "')' to close the variant pattern");
                }
                pattern->span = token.span.merge(previous().span);
                return pattern;
            }
            pattern->kind = Pattern::Kind::Binding;
            pattern->name = token.text;
            pattern->span = token.span;
            return pattern;
        }
        default:
            break;
    }

    error_at(peek(), Code::UnexpectedToken,
             std::string("expected a pattern, found ") + token_spelling(peek().kind),
             "patterns are literals, `_`, a binding name, or `Enum::Variant(...)`");
    pattern->kind = Pattern::Kind::Wildcard;
    if (!at_end()) advance();
    return pattern;
}

}  // namespace ppc
