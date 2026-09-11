contract Shape {
    fn area() -> int;
    fn name() -> str;
}
object Square meets Shape {
    let side: int;
    init(side: int) { self.side = side; }
    public fn area() -> int { return self.side * self.side; }
    public fn name() -> str { return "square"; }
}
object Rect meets Shape {
    let w: int;
    let h: int;
    init(w: int, h: int) { self.w = w; self.h = h; }
    public fn area() -> int { return self.w * self.h; }
    public fn name() -> str { return "rect"; }
}
object Dot meets Shape {
    let ignored: int;
    init() { self.ignored = 0; }
    public fn area() -> int { return 0; }
    public fn name() -> str { return "dot"; }
}

launch {
    // The thing that was impossible: one list, several concrete types.
    let shapes = list<Shape>();
    list_push(shapes, Square(4));
    list_push(shapes, Rect(3, 5));
    list_push(shapes, Dot());

    let mut total = 0;
    for s in shapes {
        say(concat(concat(s.name(), " = "), text(s.area())));
        total = total + s.area();
    }
    say(concat("total = ", text(total)));
}
