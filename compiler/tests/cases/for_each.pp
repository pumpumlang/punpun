// `for element in sequence` over every sequence kind, including a list of
// aggregates and a borrowed view. Each must visit elements in order.
struct P { x: int }

launch {
    let ns = numbers();
    for i in 0..4 { push(ns, i * 10); }
    for n in ns { print(n); print(" "); }
    say("");

    let ps = list<P>();
    list_push(ps, P(1));
    list_push(ps, P(2));
    let mut total = 0;
    for p in ps { total = total + p.x; }
    say(total);

    let names = list<str>();
    list_push(names, "alpha");
    list_push(names, "beta");
    for name in names { say(name); }

    let lit = [7, 8, 9];
    for v in view(lit, 1, 3) { print(v); print(" "); }
    say("");

    // An empty sequence runs the body zero times.
    let empty = list<int>();
    for e in empty { say(e); }
    say("done");
}
