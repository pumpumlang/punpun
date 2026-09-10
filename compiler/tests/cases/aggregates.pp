struct Point { x: int, y: int }

object Counter {
    let total: int;
    init() { self.total = 0; }
    public fn bump(by: int) { self.total = self.total + by; }
    public fn value() -> int { return self.total; }
}

struct Wrapper { inner: Point, tag: str }

launch {
    let mut p = Point(3, 4);
    say(p.x + p.y);
    p.x = 10;
    say(p.x);
    let c = Counter();
    c.bump(5); c.bump(7);
    say(c.value());
    let mut w = Wrapper(Point(1, 2), "outer");
    w.inner.y = 40;
    say(w.inner.x + w.inner.y);
    say(w.tag);
    let named = Point(y: 8, x: 1);
    say(named.x); say(named.y);
}
