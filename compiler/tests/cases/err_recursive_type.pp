// A type that embeds itself has no finite layout. This crashed the compiler
// with a stack overflow before the front-end check existed, so the case is
// pinned here.
enum Expr { Num(int), Add(Expr, Expr) }

fn eval(e: Expr) -> int {
    return match e { Expr::Num(n) => n, Expr::Add(a, b) => eval(a) + eval(b) };
}

launch { say(eval(Expr::Num(1))); }
