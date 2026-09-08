enum Choice<T> {
    Empty,
    Value(T),
}

enum Nested {
    Item(Option<int>),
}

fn probe(flag: bool) -> Result<bool, str> {
    if flag { return Result::Ok(true); }
    return Result::Error("bad");
}

fn convert(flag: bool) -> Result<int, str> {
    let value = probe(flag)?;
    return Result::Ok(match value { true => 7, false => 9 });
}

fn maybe(flag: bool) -> Option<int> {
    if flag { return Option::Some(8); }
    return Option::None;
}

fn main() {
    println(match Choice::Value(3) { Choice::Empty => 0, Choice::Value(v) => v });
    println(match Nested::Item(Option::Some(4)) {
        Nested::Item(Option::Some(v)) => v,
        Nested::Item(Option::None) => 0,
    });
    println(match convert(true) { Result::Ok(v) => v, Result::Error(e) => 0 });
    println(match convert(false) { Result::Ok(v) => v, Result::Error(e) => 55 });
    println(match maybe(false) { Option::Some(v) => v, Option::None => 66 });
}
