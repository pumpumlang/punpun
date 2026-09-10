// The shape a PunPun-written lexer needs: a growable list of tagged tokens.
enum Token { Number(int), Name(str), Symbol(str) }

fn describe(t: Token) -> str {
    return match t {
        Token::Number(n) => "number " + text(n),
        Token::Name(s) => "name " + s,
        Token::Symbol(s) => "symbol " + s,
    };
}

launch {
    let tokens = list<Token>();
    list_push(tokens, Token::Name("launch"));
    list_push(tokens, Token::Symbol("{"));
    list_push(tokens, Token::Number(42));
    list_push(tokens, Token::Symbol("}"));
    for i in 0..list_size(tokens) { say(describe(list_at(tokens, i))); }
}
