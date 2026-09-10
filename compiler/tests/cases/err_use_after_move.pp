object Handle { let id: int; init(id: int) { self.id = id; } }
fn take(h: Handle) {}
launch { let h = Handle(1); take(move(h)); take(move(h)); }
