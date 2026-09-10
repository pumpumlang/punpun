object Resource {
    let tag: str;
    init(tag: str) { self.tag = tag; }
    public fn tag_of() -> str { return self.tag; }
}

fn consume(r: Resource) -> str { return r.tag_of(); }

launch {
    let mut r = Resource("first");
    say(consume(move(r)));
    // Whole-value assignment revives a moved-out binding.
    r = Resource("second");
    say(consume(move(r)));

    let list = numbers();
    push(list, 3);
    {
        // A borrow is live until its binding leaves scope, so the list is
        // frozen for exactly this block.
        let borrowed = view(list, 0, 1);
        say(slice_len(borrowed));
        say(slice_get(borrowed, 0));
    }
    // The borrow is gone, so mutating the source is allowed again.
    push(list, 4);
    say(size(list));
}
