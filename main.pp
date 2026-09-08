import std.math;
import std.stats;
import std.text;
import std.nums;
import std.time;

fn main() {
    let started = clock_ms();
    let values = [5, 8, 13, 21, 34];
    let reversed = nums_reverse(values);

    println("PunPun 0.7 development compiler is alive.");
    println("sum = " + text(stats_sum(values)));
    println("mean = ");
    println(stats_mean(values));
    println("gcd(84, 30) = " + text(gcd(84, 30)));
    println("contains 13 = ");
    println(nums_contains(values, 13));
    println("first reversed value = " + text(reversed[0]));
    println("trimmed = '" + text_trim("  hello PunPun  ") + "'");
    println("startup work took " + text(elapsed_ms(started)) + " ms");
}
