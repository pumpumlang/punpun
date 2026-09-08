fn main() {
    let mut values = numbers();
    push(values, 10);
    push(values, 20);
    push(values, 30);
    let middle: Slice<int> = view(values, 1, 3);
    println(slice_len(middle));
    println(slice_get(middle, 0));
    println(slice_get(middle, 1));
}
