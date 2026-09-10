struct Point { x: int, y: int }
struct Wrapper { inner: Point, tag: str }

object Cell { let n: int; init(n: int) { self.n = n; } public fn get() -> int { return self.n; } public fn set(v: int) { self.n = v; } }

fn mutate(p: Point) -> int { return p.x; }

launch {
    // A struct assignment copies.
    let a = Point(1, 2);
    let mut b = a;
    b.x = 99;
    say(a.x); say(b.x);

    // A nested struct copies all the way down.
    let w = Wrapper(a, "first");
    let mut w2 = w;
    w2.inner.y = 77;
    say(w.inner.y); say(w2.inner.y);

    // Reading a nested field yields a copy.
    let mut lifted = w.inner;
    lifted.x = 55;
    say(w.inner.x); say(lifted.x);

    // Passing to a function copies.
    let mut arg = Point(7, 8);
    say(mutate(arg)); say(arg.x);

    // An object is a handle: a method writes through it, and the write is
    // visible afterwards even though the binding is not declared mutable.
    // (Binding an object to a second name would move it, so identity is shown
    // through mutation rather than aliasing.)
    let c = Cell(3);
    c.set(42);
    say(c.get());
}
