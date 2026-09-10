// Mathematical and physical constants.
//
// Written to 17 significant digits, which is what round-trips an IEEE double
// exactly. Fewer digits would silently lose precision.

fn pi() -> float { return 3.1415926535897931; }
fn tau() -> float { return 6.2831853071795862; }
fn half_pi() -> float { return 1.5707963267948966; }
fn e() -> float { return 2.7182818284590451; }
fn sqrt2() -> float { return 1.4142135623730951; }
fn sqrt3() -> float { return 1.7320508075688772; }
fn golden_ratio() -> float { return 1.6180339887498949; }
fn ln2() -> float { return 0.69314718055994531; }
fn ln10() -> float { return 2.3025850929940459; }

/// The largest and smallest values an int can hold. Written as expressions
/// rather than literals because the most negative literal cannot be written
/// directly: it is parsed as a negation of a value that does not fit.
fn int_max() -> int { return 9223372036854775807; }
fn int_min() -> int { return 0 - 9223372036854775807 - 1; }

fn epsilon() -> float { return 0.000000000000000222044604925031; }

fn degrees_to_radians(degrees: float) -> float { return degrees * pi() / 180.0; }
fn radians_to_degrees(radians: float) -> float { return radians * 180.0 / pi(); }
