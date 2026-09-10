object Resource { let id: int; init(id: int) { self.id = id; } }
fn duplicate<T: Copy>(value: T) -> T { return value; }
launch { let r = Resource(1); say(1); duplicate(r); }
