enum State { Ready, Busy }

fn main() {
    let state = State::Ready;
    println(match state { State::Ready => 1 });
}
