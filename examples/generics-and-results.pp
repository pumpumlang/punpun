bring std::option
bring std::result

struct Box<T> {
    value: T,

    fn get() -> T {
        return self.value;
    }
}

fn checked(flag: bool) -> Result<int, str> {
    if flag { return Result::Ok(42); }
    return Result::Error("not ready");
}

fn main() {
    let box = Box("PunPun");
    println(box.get());

    let answer = checked(true);
    println(result_unwrap_or(answer, 0));

    let maybe: Option<int> = Option::None;
    println(option_unwrap_or(maybe, 7));
}
