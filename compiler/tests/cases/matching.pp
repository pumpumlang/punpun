enum Shape { Circle(int), Rect(int, int), Empty }
enum Wrap { Held(Option<int>) }

fn area(s: Shape) -> int {
    return match s {
        Shape::Circle(r) => 3 * r * r,
        Shape::Rect(w, h) => w * h,
        Shape::Empty => 0,
    };
}

fn attempt(ok: bool) -> Result<int, str> {
    if ok { return Result::Ok(4); }
    return Result::Error("nope");
}

fn chained(ok: bool) -> Result<int, str> {
    let value = attempt(ok)?;
    return Result::Ok(value * 10);
}

launch {
    say(area(Shape::Circle(2)));
    say(area(Shape::Rect(3, 5)));
    say(area(Shape::Empty));
    say(match Wrap::Held(Option::Some(7)) {
        Wrap::Held(Option::Some(v)) => v,
        Wrap::Held(Option::None) => -1,
    });
    say(match Wrap::Held(Option::None) {
        Wrap::Held(Option::Some(v)) => v,
        Wrap::Held(Option::None) => -1,
    });
    say(match chained(true) { Result::Ok(v) => v, Result::Error(e) => 0 });
    say(match chained(false) { Result::Ok(v) => v, Result::Error(e) => -7 });
    say(match 5 { 1 => "one", 5 => "five", _ => "other" });
    say(match "beta" { "alpha" => 1, "beta" => 2, _ => 3 });
    say(match true { true => "t", false => "f" });
}
