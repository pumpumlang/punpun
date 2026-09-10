enum State { Ready, Busy, Done }
fn describe(s: State) -> int { return match s { State::Ready => 1, State::Busy => 2 }; }
launch { say(describe(State::Ready)); }
