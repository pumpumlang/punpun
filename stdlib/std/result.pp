# Generic Result helpers. Result<T,E> itself is a prelude algebraic enum.

fn result_is_ok<T, E>(value: Result<T, E>) -> bool {
    return match value {
        Result::Ok(item) => true,
        Result::Error(error) => false,
    };
}

fn result_is_error<T, E>(value: Result<T, E>) -> bool {
    return not result_is_ok(value);
}

fn result_unwrap_or<T: Copy, E>(value: Result<T, E>, fallback: T) -> T {
    return match value {
        Result::Ok(item) => item,
        Result::Error(error) => fallback,
    };
}
