// Recursion through an identity object is fine: the handle is one word wide, so
// the layout is finite. This is the shape the recursion check must NOT reject.
object Node {
    let value: int;
    init(value: int) { self.value = value; }
    public fn get() -> int { return self.value; }
}

struct Pair { left: int, right: int }

launch {
    let n = Node(7);
    say(n.get());
    let p = Pair(1, 2);
    say(p.left + p.right);
}
