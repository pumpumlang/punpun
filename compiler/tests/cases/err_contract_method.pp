contract Shape { fn area() -> int; }
object Sq meets Shape {
    let side: int;
    init(side: int) { self.side = side; }
    public fn area() -> int { return self.side * self.side; }
}
fn wrong(s: Shape) -> int { return s.perimeter(); }
launch { say(wrong(Sq(2))); }
