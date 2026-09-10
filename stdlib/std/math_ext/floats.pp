// Float helpers.

fn float_min(a: float, b: float) -> float { if a < b { return a; } return b; }
fn float_max(a: float, b: float) -> float { if a > b { return a; } return b; }

fn float_clamp(value: float, low: float, high: float) -> float {
    if low > high { panic("clamp needs low <= high"); }
    if value < low { return low; }
    if value > high { return high; }
    return value;
}

/// Comparison with an absolute tolerance. Direct == on floats is almost always
/// wrong after arithmetic, because the result carries rounding error.
fn nearly_equal(a: float, b: float, tolerance: float) -> bool {
    return fabs(a - b) <= tolerance;
}

fn lerp(a: float, b: float, t: float) -> float { return a + (b - a) * t; }

/// Where `value` sits between `low` and `high`, as a fraction. The inverse of
/// lerp.
fn inverse_lerp(low: float, high: float, value: float) -> float {
    if high == low { return 0.0; }
    return (value - low) / (high - low);
}

fn remap(value: float, from_low: float, from_high: float,
         to_low: float, to_high: float) -> float {
    return lerp(to_low, to_high, inverse_lerp(from_low, from_high, value));
}

/// Smooth interpolation with zero derivative at both ends, the usual choice for
/// animation because linear interpolation visibly starts and stops abruptly.
fn smoothstep(low: float, high: float, value: float) -> float {
    let t = float_clamp(inverse_lerp(low, high, value), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

fn round_to(value: float, places: int) -> float {
    let mut scale = 1.0;
    for i in 0..places { scale = scale * 10.0; }
    return round(value * scale) / scale;
}

fn truncate(value: float) -> float {
    if value < 0.0 { return ceil(value); }
    return floor(value);
}

fn float_sign(value: float) -> float {
    if value > 0.0 { return 1.0; }
    if value < 0.0 { return 0.0 - 1.0; }
    return 0.0;
}
