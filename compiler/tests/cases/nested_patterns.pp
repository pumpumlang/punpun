// Regression: a nested pattern must test the inner discriminant BEFORE
// projecting a payload out of it. Extracting bindings first and testing
// afterwards reads a payload slot the actual variant does not have, which is a
// heap overflow on the boxing backends and an uninitialized read under C.
enum Wrap { Held(Option<int>), Bare }
enum Deep { Outer(Result<Option<int>, str>) }

fn probe(w: Wrap) -> int {
    return match w {
        Wrap::Held(Option::Some(v)) => v,
        Wrap::Held(Option::None) => -1,
        Wrap::Bare => -2,
    };
}

fn deeper(d: Deep) -> int {
    return match d {
        Deep::Outer(Result::Ok(Option::Some(v))) => v,
        Deep::Outer(Result::Ok(Option::None)) => -1,
        Deep::Outer(Result::Error(e)) => -2,
    };
}

launch {
    // The None case is the one that overflowed: arm 1 is tried first and must
    // abandon before binding `v`.
    say(probe(Wrap::Held(Option::None)));
    say(probe(Wrap::Held(Option::Some(7))));
    say(probe(Wrap::Bare));

    // Three levels deep, same requirement at each level.
    say(deeper(Deep::Outer(Result::Ok(Option::None))));
    say(deeper(Deep::Outer(Result::Ok(Option::Some(9)))));
    say(deeper(Deep::Outer(Result::Error("bad"))));

    // A guard runs after the bindings it may read, but only once the arm's
    // tests have all passed.
    say(match Wrap::Held(Option::Some(3)) {
        Wrap::Held(Option::Some(v)) if v > 5 => 100,
        Wrap::Held(Option::Some(v)) => v * 2,
        Wrap::Held(Option::None) => 0,
        Wrap::Bare => 0,
    });
}
