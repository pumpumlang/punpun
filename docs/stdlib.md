# Standard Library

## Generic sum types

`Option<T>` and `Result<T,E>` are available from the prelude. Import
`std::option` for `option_is_some`, `option_is_none`, and `option_unwrap_or`.
Import `std::result` for `result_is_ok`, `result_is_error`, and
`result_unwrap_or`. The `*_unwrap_or` helpers require a `Copy` success type.

The `stdlib/` import root is included automatically. Modules are ordinary PunPun
source, with no entry point or global constants. Import only what you need:

```punpun
bring std.math
bring std.stats
bring std.text
bring std.nums
bring std.time

launch:
    pin readings <- [9, 2, 7]
    say clamp(stats_sum(readings), 0, 100)
    say text_trim("  ready\n")
done
```

All modules share the program's global function/shape namespace. Functions are
called by their unqualified names; `stats_` and `text_` prefixes avoid collisions
with built-ins and the math module. No import aliasing or module-qualified calls
are available. All imported declarations are validated, even when unused.

## `std.math`

Source: [`stdlib/std/math.pp`](../stdlib/std/math.pp).

| Function | Contract |
| --- | --- |
| `minimum(left as int, right as int) gives int` | Smaller operand |
| `maximum(left as int, right as int) gives int` | Larger operand |
| `clamp(value as int, lower as int, upper as int) gives int` | Clamp to the inclusive interval; panic if `lower > upper` |
| `gcd(left as int, right as int) gives int` | Nonnegative Euclidean GCD; accepts negative inputs; `gcd(0, 0)` is `0` |
| `factorial(n as int) gives int` | `n!` for `0 <= n <= 20`; otherwise panic; `0!` is `1` |
| `integer_power(base as int, exponent as int) gives int` | Exponentiation by squaring; nonnegative exponent required; exponent `0` returns `1`, including `0^0` |

Arithmetic is checked, including intermediate products. `integer_power` panics
on overflow rather than wrapping. `gcd` works with negative magnitudes internally
so the smallest signed integer is accepted when the result is representable;
`gcd(-9223372036854775808, 2)` returns `2`, while a mathematical result of `2^63`
panics because it cannot be returned as a nonnegative `int`.

`minimum`, `maximum`, and `clamp` take constant work. GCD uses the Euclidean
algorithm; power takes logarithmically many iterations in the exponent. These
functions allocate no lists or generated strings on successful calls.

## `std.stats`

Source: [`stdlib/std/stats.pp`](../stdlib/std/stats.pp).

| Function | Contract |
| --- | --- |
| `stats_sum(values as nums) gives int` | Checked left-to-right sum; empty list returns `0` |
| `stats_mean(values as nums) gives float` | Checked integer sum converted to `float`, divided by converted length; empty list panics |
| `stats_min(values as nums) gives int` | Smallest element; empty list panics |
| `stats_max(values as nums) gives int` | Largest element; empty list panics |
| `stats_sorted(values as nums) gives nums` | New ascending sorted copy; accepts an empty list; does not mutate its input |

Sum, mean, minimum, and maximum scan the list in linear time and do not mutate it.
`stats_sum` can overflow on an intermediate sum even if a different summation order
would fit. `stats_mean` has the same overflow restriction, even if the mathematical
mean would fit; conversion to `float` may lose precision for large integers.

`stats_sorted` copies the elements into a new list and invokes the runtime sort.
It requires storage proportional to the list length. For an in-place sort, use
the built-in `sort(values)` instead; every alias will observe that mutation.

## `std.text`

Source: [`stdlib/std/text.pp`](../stdlib/std/text.pp).

| Function | Contract |
| --- | --- |
| `text_starts_with(value as str, prefix as str) gives bool` | Case-sensitive byte prefix match; an empty prefix matches |
| `text_ends_with(value as str, suffix as str) gives bool` | Case-sensitive byte suffix match; an empty suffix matches |
| `text_trim(value as str) gives str` | Remove leading/trailing ASCII space, tab, CR, and LF; all-whitespace input returns `""` |
| `text_repeat(value as str, count as int) gives str` | Concatenate `count` copies; `0` returns `""`; negative counts panic |

Prefix and suffix helpers short-circuit before slicing when the candidate is
longer than the input. They allocate a slice when comparing. Trimming uses
one-byte slices and a final result slice; it does not implement Unicode whitespace
rules. String operations are byte-based, not code-point-based.

`text_repeat` is a small-input convenience, not a string builder: repeated
concatenation copies growing prefixes. Work and retained intermediate string
storage can grow quadratically in the count for nonempty input. Trimming also
retains its temporary slices until normal exit. The runtime cleans allocations
at normal process exit, not when these functions return; avoid unbounded use in
long-lived workloads.


## `std.nums`

Source: [`stdlib/std/nums.pp`](../stdlib/std/nums.pp).

| Function | Contract |
| --- | --- |
| `nums_copy(values as nums) gives nums` | New list with the same elements |
| `nums_contains(values as nums, needle as int) gives bool` | `yes` when any element equals `needle` |
| `nums_index_of(values as nums, needle as int) gives int` | First matching index, or `-1` |
| `nums_count(values as nums, needle as int) gives int` | Number of matching elements |
| `nums_reverse(values as nums) gives nums` | New list in reverse order |
| `nums_equal(left as nums, right as nums) gives bool` | Same length and same elements in the same order |

All helpers leave the input list unchanged. Copy and reverse allocate a new `nums`
handle; the search/count/equality helpers are linear scans.

## `std.time`

Source: [`stdlib/std/time.pp`](../stdlib/std/time.pp).

| Function | Contract |
| --- | --- |
| `elapsed_ms(start as int) gives int` | Monotonic milliseconds elapsed since a prior `clock_ms()` value |
| `deadline_reached(deadline as int) gives bool` | Whether monotonic time has reached a deadline |
| `deadline_after_ms(duration as int) gives int` | Create a deadline `duration` milliseconds from now; negative durations panic |

These helpers use the monotonic `clock_ms()` runtime primitive. They are for durations
and deadlines, not wall-clock dates or timestamps.

## `std.testing`

Source: [`stdlib/std/testing.pp`](../stdlib/std/testing.pp).

| Function | Contract |
| --- | --- |
| `expect_int(actual as int, expected as int) gives void` | Assert integer equality with values in the failure text |
| `expect_bool(actual as bool, expected as bool) gives void` | Assert boolean equality |
| `expect_str(actual as str, expected as str) gives void` | Assert string equality |

The testing module is intentionally tiny. Failures use the built-in `assert`, so a
failing expectation prints a PunPun panic and aborts the process.
