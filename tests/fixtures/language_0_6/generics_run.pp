fn identity<T: Copy>(value: T) -> T {
    return value;
}

struct Box<T> {
    value: T,

    public fn get() -> T {
        return self.value;
    }

    fn echo<U: Copy>(other: U) -> U {
        return other;
    }
}

object Cell<T> {
    let value: T;

    init(value: T) {
        self.value = value;
    }

    public fn get() -> T {
        return self.value;
    }
}

contract Named {
    fn name() -> str;
}

object Label meets Named {
    init() {}
    public fn name() -> str { return "contract"; }
}

fn generic_name<T: Named>(value: T) -> str {
    return value.name();
}

fn main() {
    let box: Box<int> = Box(9);
    println(identity<int>(box.get()));
    println(box.echo(12));
    println(identity("generic"));
    let cell: Cell<int> = Cell<int>(14);
    println(cell.get());
    let label = Label();
    println(generic_name(label));
}
