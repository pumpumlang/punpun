import std.data.hex
import std.data.base64

# Deterministic Park-Miller RNG written in PunPun. The Schrage step avoids
# overflow under PunPun's checked integer arithmetic.
object Random {
    public let state: int;
    public init(seed: int) {
        let mut normalized = seed % 2147483647;
        if normalized <= 0 { normalized = normalized + 2147483646; }
        self.state = normalized;
    }
    public fn next_raw() -> int {
        let high = self.state / 127773;
        let low = self.state % 127773;
        let mut value = 16807 * low - 2836 * high;
        if value <= 0 { value = value + 2147483647; }
        self.state = value;
        return value;
    }
    public fn int_between(low: int, high: int) -> int {
        if low > high { panic("Random.int_between requires low <= high"); }
        let span = high - low + 1;
        return low + (self.next_raw() % span);
    }
    public fn unit() -> float { return decimal(self.next_raw()) / 2147483647.0; }
    public fn chance(probability: float) -> bool { return self.unit() < probability; }
}

fn random_generator(seed: int) -> Random { return Random(seed); }

fn secure_token_hex(byte_count: int) -> str {
    if byte_count < 0 { panic("secure_token_hex requires a nonnegative size"); }
    return hex_encode(random_bytes(byte_count));
}

fn secure_token_urlsafe(byte_count: int) -> str {
    let encoded = base64_encode(random_bytes(byte_count));
    return replace(replace(replace(encoded, "+", "-"), "/", "_"), "=", "");
}
