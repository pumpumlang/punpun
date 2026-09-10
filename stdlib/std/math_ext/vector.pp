// 2D and 3D vectors with value semantics.

struct Vec2 { x: float, y: float }
struct Vec3 { x: float, y: float, z: float }

fn vec2(x: float, y: float) -> Vec2 { return Vec2(x, y); }
fn vec2_zero() -> Vec2 { return Vec2(0.0, 0.0); }

fn vec2_add(a: Vec2, b: Vec2) -> Vec2 { return Vec2(a.x + b.x, a.y + b.y); }
fn vec2_sub(a: Vec2, b: Vec2) -> Vec2 { return Vec2(a.x - b.x, a.y - b.y); }
fn vec2_scale(a: Vec2, factor: float) -> Vec2 { return Vec2(a.x * factor, a.y * factor); }
fn vec2_dot(a: Vec2, b: Vec2) -> float { return a.x * b.x + a.y * b.y; }
fn vec2_length(a: Vec2) -> float { return hypot(a.x, a.y); }
fn vec2_length_squared(a: Vec2) -> float { return a.x * a.x + a.y * a.y; }

fn vec2_normalize(a: Vec2) -> Vec2 {
    let length = vec2_length(a);
    // Returning zero rather than trapping: a zero vector has no direction, and
    // callers normalizing a possibly-zero vector would otherwise all need the
    // same guard.
    if length == 0.0 { return vec2_zero(); }
    return vec2_scale(a, 1.0 / length);
}

fn vec2_distance(a: Vec2, b: Vec2) -> float { return vec2_length(vec2_sub(a, b)); }
fn vec2_angle(a: Vec2) -> float { return atan2(a.y, a.x); }

fn vec2_lerp(a: Vec2, b: Vec2, t: float) -> Vec2 {
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

fn vec2_rotate(a: Vec2, radians: float) -> Vec2 {
    let c = cos(radians);
    let s = sin(radians);
    return Vec2(a.x * c - a.y * s, a.x * s + a.y * c);
}

fn vec2_to_text(a: Vec2) -> str {
    return "(" + text_float(a.x) + ", " + text_float(a.y) + ")";
}

fn vec3(x: float, y: float, z: float) -> Vec3 { return Vec3(x, y, z); }
fn vec3_zero() -> Vec3 { return Vec3(0.0, 0.0, 0.0); }

fn vec3_add(a: Vec3, b: Vec3) -> Vec3 { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
fn vec3_sub(a: Vec3, b: Vec3) -> Vec3 { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
fn vec3_scale(a: Vec3, factor: float) -> Vec3 {
    return Vec3(a.x * factor, a.y * factor, a.z * factor);
}
fn vec3_dot(a: Vec3, b: Vec3) -> float { return a.x * b.x + a.y * b.y + a.z * b.z; }

fn vec3_cross(a: Vec3, b: Vec3) -> Vec3 {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

fn vec3_length(a: Vec3) -> float { return sqrt(vec3_dot(a, a)); }

fn vec3_normalize(a: Vec3) -> Vec3 {
    let length = vec3_length(a);
    if length == 0.0 { return vec3_zero(); }
    return vec3_scale(a, 1.0 / length);
}

fn vec3_distance(a: Vec3, b: Vec3) -> float { return vec3_length(vec3_sub(a, b)); }

fn vec3_to_text(a: Vec3) -> str {
    return "(" + text_float(a.x) + ", " + text_float(a.y) + ", " + text_float(a.z) + ")";
}
