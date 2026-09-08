object Resource {
    init() {}
}

fn duplicate<T: Copy>(value: T) -> T {
    return value;
}

fn main() {
    let resource = Resource();
    let copied = duplicate(resource);
    drop(copied);
}
