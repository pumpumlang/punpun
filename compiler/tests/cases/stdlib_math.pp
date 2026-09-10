import std.math_ext.integers
import std.math_ext.bits
import std.math_ext.vector
import std.math_ext.constants
import std.math_ext.floats
import std.math_ext.statistics

launch {
    say(gcd(48, 18)); say(lcm(4, 6)); say(is_prime(97));
    say(list_size(primes_up_to(100))); say(factorial(10));
    say(pow_mod(2, 10, 1000)); say(int_sqrt(99)); say(fibonacci(20));
    say(digit_sum(12345)); say(int_pow(3, 5));
    say(popcount(255)); say(to_binary(10)); say(from_binary("1010"));
    say(is_power_of_two(64)); say(next_power_of_two(100));
    say(trailing_zeros(8)); say(bit_get(5, 0));
    let a = vec2(3.0, 4.0);
    say(vec2_length(a)); say(vec2_dot(a, vec2(1.0, 0.0)));
    say(vec3_length(vec3_cross(vec3(1.0,0.0,0.0), vec3(0.0,1.0,0.0))));
    say(round_to(pi(), 4)); say(nearly_equal(0.1 + 0.2, 0.3, 0.0001));
    say(smoothstep(0.0, 1.0, 0.5));
    let d = list<float>();
    list_push(d, 4.0); list_push(d, 1.0); list_push(d, 3.0); list_push(d, 2.0);
    say(mean(d)); say(median(d)); say(max_float_of(d)); say(round_to(standard_deviation(d), 4));
}
