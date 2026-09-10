// Identifier case conversion.

fn to_snake_case(value: str) -> str {
    let mut out = "";
    for i in 0..len(value) {
        let code = char_at(value, i);
        if code >= 65 and code <= 90 {
            // Separate before an uppercase letter, but not at the very start,
            // or "Name" would become "_name".
            if i > 0 { out = out + "_"; }
            out = out + char_str(code + 32);
        } else if code == 32 or code == 45 {
            out = out + "_";
        } else {
            out = out + char_str(code);
        }
    }
    return out;
}

fn to_kebab_case(value: str) -> str {
    return replace(to_snake_case(value), "_", "-");
}

fn to_camel_case(value: str) -> str {
    let parts = split(replace(replace(value, "-", "_"), " ", "_"), "_");
    let mut out = "";
    for i in 0..list_size(parts) {
        let piece = list_at(parts, i);
        if len(piece) == 0 { continue; }
        if len(out) == 0 {
            out = to_lower(piece);
        } else {
            out = out + to_upper(slice(piece, 0, 1)) + to_lower(slice(piece, 1, len(piece)));
        }
    }
    return out;
}

fn to_pascal_case(value: str) -> str {
    let camel = to_camel_case(value);
    if len(camel) == 0 { return camel; }
    return to_upper(slice(camel, 0, 1)) + slice(camel, 1, len(camel));
}

fn to_screaming_snake_case(value: str) -> str {
    return to_upper(to_snake_case(value));
}
