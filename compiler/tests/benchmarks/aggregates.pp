// Value-semantics stress: struct copies, nested field access, copy elision
// opportunities. This is what escape analysis should improve.
struct Point { x: int, y: int }
struct Box { corner: Point, size: Point, tag: int }

fn area(b: Box) -> int { return b.size.x * b.size.y; }
fn shift(b: Box, dx: int) -> Box { return Box(Point(b.corner.x + dx, b.corner.y), b.size, b.tag); }

launch {
    let mut total = 0;
    for i in 0..40000 {
        let b = Box(Point(i, i + 1), Point(3, 4), i);
        let moved = shift(b, 2);
        total = total + area(moved) + moved.corner.x;
    }
    say(total);
}
