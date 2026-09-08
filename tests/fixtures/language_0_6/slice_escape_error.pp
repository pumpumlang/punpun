fn invalid_view() -> Slice<int> {
    let values = numbers();
    push(values, 1);
    return view(values, 0, 1);
}
fn main() {}
