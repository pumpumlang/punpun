# Generic Option helpers. Option<T> itself is a prelude algebraic enum.

fn option_is_some<T>(value: Option<T>) -> bool {
    return match value {
        Option::Some(item) => true,
        Option::None => false,
    };
}

fn option_is_none<T>(value: Option<T>) -> bool {
    return not option_is_some(value);
}

fn option_unwrap_or<T: Copy>(value: Option<T>, fallback: T) -> T {
    return match value {
        Option::Some(item) => item,
        Option::None => fallback,
    };
}
