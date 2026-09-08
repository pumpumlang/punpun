fn main() {
    let mut values = numbers();
    push(values, 1);
    let window: Slice<int> = view(values, 0, 1);
    push(values, 2);
    println(slice_get(window, 0));
}
