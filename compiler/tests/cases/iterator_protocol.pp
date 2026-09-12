// Structural iteration protocol:
//   iterable.iter() -> iterator
//   iterator.advance() -> Option<T>
// An iterator object may also be used directly.
object CounterIter {
    let current: int;
    let end: int;

    init(current: int, end: int) {
        self.current = current;
        self.end = end;
    }

    public fn advance() -> Option<int> {
        if self.current >= self.end { return Option::None; }
        let value = self.current;
        self.current = self.current + 1;
        return Option::Some(value);
    }
}

struct Counter {
    start: int,
    end: int,

    public fn iter() -> CounterIter {
        return CounterIter(self.start, self.end);
    }
}

object RepeatIter<T: Copy> {
    let value: T;
    let remaining: int;

    init(value: T, remaining: int) {
        self.value = value;
        self.remaining = remaining;
    }

    public fn advance() -> Option<T> {
        if self.remaining <= 0 { return Option::None; }
        self.remaining = self.remaining - 1;
        return Option::Some(self.value);
    }
}

struct Repeat<T: Copy> {
    value: T,
    count: int,

    public fn iter() -> RepeatIter<T> {
        return RepeatIter<T>(self.value, self.count);
    }
}

launch {
    // A normal user-defined iterable.
    let counter = Counter(2, 7);
    for value in counter {
        if value == 3 { continue; }
        print(value);
        print(" ");
        if value == 5 { break; }
    }
    say("");

    // Iterator objects are iterable directly; no wrapper type is required.
    let direct = CounterIter(7, 10);
    for value in direct { print(value); }
    say("");

    // Owner generics flow through iter() and advance() to infer the element.
    let repeated = Repeat<str>("ha", 3);
    for word in repeated { print(word); }
    say("");

    // Existing built-in sequence lowering remains compatible.
    let xs = list<int>();
    list_push(xs, 4);
    list_push(xs, 5);
    for x in xs { print(x); }
    say("");
}
