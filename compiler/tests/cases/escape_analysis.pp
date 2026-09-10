// Correctness contract for escape analysis and copy elision.
//
// Every case here has an answer that changes if a promoted allocation is
// wrongly shared, or if an elided copy aliases something it should not.
struct Point { x: int, y: int }
struct Box { corner: Point, tag: int }
object Cell { let n: int; init(n: int) { self.n = n; } public fn get() -> int { return self.n; } }

fn read_only(b: Box) -> int { return b.corner.x + b.tag; }
fn escaping() -> Point { return Point(9, 8); }
fn mutating(b: Box) -> int { let mut copy = b; copy.tag = 999; return copy.tag; }

launch {
    // Local, never escapes: promoted to the frame.
    let p = Point(1, 2);
    say(p.x + p.y);

    // Promoted allocation inside a loop: frame storage is reused every
    // iteration, so it must be reinitialized rather than carrying stale values.
    let mut total = 0;
    for i in 0..3 {
        let q = Point(i, i * 10);
        total = total + q.x + q.y;
    }
    say(total);

    // Passed to a call: the callee gets a copy, the original stays local.
    let b = Box(Point(5, 6), 7);
    say(read_only(b));
    say(b.tag);

    // The callee mutating its own copy must not touch the caller's value.
    say(mutating(b));
    say(b.tag);

    // Returned: genuinely escapes, must stay on the heap.
    say(escaping().x);

    // Nested value struct copied out of a container is independent.
    let mut lifted = b.corner;
    lifted.x = 100;
    say(b.corner.x);
    say(lifted.x);

    // Identity objects must never be promoted, whatever the analysis says.
    let c = Cell(3);
    say(c.get());
}
