fn identity<T: Copy>(value: T) -> T { return value; }
fn pair_sum<T: Copy>(a: T, b: T) -> T { return a; }

struct Box<T> {
    value: T,
    public fn get() -> T { return self.value; }
}

enum Holder<T> { Nothing, Just(T) }

fn unwrap<T: Copy>(holder: Holder<T>, fallback: T) -> T {
    return match holder { Holder::Nothing => fallback, Holder::Just(v) => v };
}

launch {
    say(identity<int>(5));
    say(identity("text"));
    say(identity(2.5));
    let b: Box<int> = Box(11);
    say(b.get());
    let s: Box<str> = Box("boxed");
    say(s.get());
    say(unwrap(Holder::Just(3), 0));
    say(unwrap<int>(Holder::Nothing, 99));
    say(pair_sum(1, 2));
}
