// A List<T> owns its elements. Pushing copies in and reading copies out, so a
// stored element is independent of both the value that was pushed and any value
// previously read. The native and bytecode backends box aggregates, so getting
// this wrong there produces silent aliasing rather than an error — which is
// exactly what happened before these cases existed.
struct P { x: int, y: int }
struct Nested { inner: P, tag: int }

launch {
    let ps = list<P>();
    let mut a = P(1, 2);
    list_push(ps, a);
    a.x = 99;
    say(list_at(ps, 0).x);

    let mut b = list_at(ps, 0);
    b.x = 77;
    say(list_at(ps, 0).x);

    // Nested aggregates must copy all the way down.
    let ns = list<Nested>();
    list_push(ns, Nested(P(5, 6), 7));
    let mut n = list_at(ns, 0);
    n.inner.y = 42;
    say(list_at(ns, 0).inner.y);

    // A list is itself a shared handle: two names see one sequence.
    let shared = ps;
    list_push(shared, P(8, 9));
    say(list_size(ps));

    // Floats round-trip their bits, not a converted value.
    let reals = list<float>();
    list_push(reals, 0.1);
    say(list_at(reals, 0) * 10.0);
}
