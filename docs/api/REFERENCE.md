# PunPun first-party API reference

Generated from the checked-in PunPun standard library and first-party package sources.
Run `pp doc` to regenerate or `pp doc --check` in CI.

## `packages/cli/src/main.pp`

### `cli_count`

```punpun
fn cli_count() -> i64
```

### `cli_arg`

```punpun
fn cli_arg(index: i64) -> String
```

### `cli_has_args`

```punpun
fn cli_has_args() -> bool
```

## `packages/filesystem/src/main.pp`

### `file_read`

```punpun
fn file_read(path: String) -> String
```

### `file_write`

```punpun
fn file_write(path: String, contents: String)
```

### `file_present`

```punpun
fn file_present(path: String) -> bool
```

### `working_directory`

```punpun
fn working_directory() -> String
```

## `packages/json/src/main.pp`

### `pp_json_valid`

```punpun
extern native fn pp_json_valid(text: String) -> i64;
```

### `pp_json_get_string`

```punpun
extern native fn pp_json_get_string(text: String, key: String, fallback: String) -> String;
```

### `pp_json_get_i64`

```punpun
extern native fn pp_json_get_i64(text: String, key: String, fallback: i64) -> i64;
```

### `pp_json_quote`

```punpun
extern native fn pp_json_quote(text: String) -> String;
```

### `json_valid`

```punpun
fn json_valid(text: String) -> bool
```

### `json_get_string`

```punpun
fn json_get_string(text: String, key: String, fallback: String) -> String
```

### `json_get_i64`

```punpun
fn json_get_i64(text: String, key: String, fallback: i64) -> i64
```

### `json_quote`

```punpun
fn json_quote(text: String) -> String
```

## `packages/logging/src/main.pp`

### `log_trace`

```punpun
fn log_trace(message: String)
```

### `log_debug`

```punpun
fn log_debug(message: String)
```

### `log_info`

```punpun
fn log_info(message: String)
```

### `log_warn`

```punpun
fn log_warn(message: String)
```

### `log_error`

```punpun
fn log_error(message: String)
```

## `packages/requests/src/main.pp`

### `HttpResponse`

```punpun
object HttpResponse
```

The 1.0 requests surface is preserved, but its implementation is now the compiler/runtime HTTPS service rather than package-local injected C.

### `ok`

```punpun
public fn ok() -> bool
```

### `text`

```punpun
public fn text() -> String
```

### `requests_available`

```punpun
fn requests_available() -> bool
```

### `requests_request`

```punpun
fn requests_request(method: String, url: String, body: String, headers: String,
```

### `requests_get`

```punpun
fn requests_get(url: String) -> HttpResponse
```

### `requests_post`

```punpun
fn requests_post(url: String, body: String) -> HttpResponse
```

### `requests_put`

```punpun
fn requests_put(url: String, body: String) -> HttpResponse
```

### `requests_patch`

```punpun
fn requests_patch(url: String, body: String) -> HttpResponse
```

### `requests_delete`

```punpun
fn requests_delete(url: String) -> HttpResponse
```

### `requests_head`

```punpun
fn requests_head(url: String) -> HttpResponse
```

### `requests_request_async`

```punpun
async fn requests_request_async(method: String, url: String, body: String,
```

### `requests_get_async`

```punpun
async fn requests_get_async(url: String) -> HttpResponse
```

### `requests_post_async`

```punpun
async fn requests_post_async(url: String, body: String) -> HttpResponse
```

### `requests_put_async`

```punpun
async fn requests_put_async(url: String, body: String) -> HttpResponse
```

### `requests_patch_async`

```punpun
async fn requests_patch_async(url: String, body: String) -> HttpResponse
```

### `requests_delete_async`

```punpun
async fn requests_delete_async(url: String) -> HttpResponse
```

### `requests_head_async`

```punpun
async fn requests_head_async(url: String) -> HttpResponse
```

## `packages/testing/src/main.pp`

### `expect`

```punpun
fn expect(condition: bool, message: String)
```

### `expect_equal_i64`

```punpun
fn expect_equal_i64(left: i64, right: i64, message: String)
```

### `expect_equal_string`

```punpun
fn expect_equal_string(left: String, right: String, message: String)
```

## `stdlib/std/async.pp`

### `delay_ms`

```punpun
async fn delay_ms(delay: i64)
```

PunPun structured async helpers. `async fn` / `await` are compiler features. This module provides reusable task helpers while the runtime keeps task creation lazy for ordinary programs.

### `delayed_i64`

```punpun
async fn delayed_i64(value: i64, delay: i64) -> i64
```

### `delayed_text`

```punpun
async fn delayed_text(value: String, delay: i64) -> String
```

### `read_text_async`

```punpun
async fn read_text_async(path: String) -> String
```

### `write_text_async`

```punpun
async fn write_text_async(path: String, value: String)
```

### `cancellable_delay_ms`

```punpun
async fn cancellable_delay_ms(delay: i64) -> bool
```

Cooperative workers should check cancelled() at natural loop boundaries. sleep_ms() is itself a cancellation safe point and returns early when the current task receives a cancellation request.

## `stdlib/std/collections/counter.pp`

### `counter_new`

```punpun
fn counter_new() -> Map<int>
```

### `counter_add`

```punpun
fn counter_add(counts: Map<int>, key: str)
```

### `counter_add_many`

```punpun
fn counter_add_many(counts: Map<int>, key: str, amount: int)
```

### `counter_get`

```punpun
fn counter_get(counts: Map<int>, key: str) -> int
```

### `counter_total`

```punpun
fn counter_total(counts: Map<int>) -> int
```

### `counter_most_common`

```punpun
fn counter_most_common(counts: Map<int>) -> str
```

## `stdlib/std/collections/grid.pp`

### `Grid`

```punpun
struct Grid
```

### `grid_new`

```punpun
fn grid_new(width: int, height: int, initial: int) -> Grid
```

### `grid_index`

```punpun
fn grid_index(board: Grid, x: int, y: int) -> int
```

### `grid_get`

```punpun
fn grid_get(board: Grid, x: int, y: int) -> int
```

### `grid_set`

```punpun
fn grid_set(board: Grid, x: int, y: int, value: int)
```

### `grid_in_bounds`

```punpun
fn grid_in_bounds(board: Grid, x: int, y: int) -> bool
```

### `grid_get_or`

```punpun
fn grid_get_or(board: Grid, x: int, y: int, fallback: int) -> int
```

### `grid_fill`

```punpun
fn grid_fill(board: Grid, value: int)
```

### `grid_count`

```punpun
fn grid_count(board: Grid, value: int) -> int
```

### `grid_neighbour_sum`

```punpun
fn grid_neighbour_sum(board: Grid, x: int, y: int) -> int
```

### `grid_to_text`

```punpun
fn grid_to_text(board: Grid) -> str
```

## `stdlib/std/collections/heap.pp`

### `heap_new`

```punpun
fn heap_new() -> List<int>
```

### `heap_push`

```punpun
fn heap_push(heap: List<int>, value: int)
```

### `heap_peek`

```punpun
fn heap_peek(heap: List<int>) -> int
```

### `heap_pop`

```punpun
fn heap_pop(heap: List<int>) -> int
```

### `heap_size`

```punpun
fn heap_size(heap: List<int>) -> int
```

### `heap_is_empty`

```punpun
fn heap_is_empty(heap: List<int>) -> bool
```

### `heap_sort`

```punpun
fn heap_sort(values: List<int>) -> List<int>
```

## `stdlib/std/collections/list_ops.pp`

### `list_copy`

```punpun
fn list_copy<T: Copy>(items: List<T>) -> List<T>
```

### `list_concat`

```punpun
fn list_concat<T: Copy>(left: List<T>, right: List<T>) -> List<T>
```

### `list_reverse`

```punpun
fn list_reverse<T: Copy>(items: List<T>)
```

### `list_slice`

```punpun
fn list_slice<T: Copy>(items: List<T>, start: int, end: int) -> List<T>
```

### `list_swap`

```punpun
fn list_swap<T: Copy>(items: List<T>, a: int, b: int)
```

### `list_fill`

```punpun
fn list_fill<T: Copy>(count: int, value: T) -> List<T>
```

### `list_remove_at`

```punpun
fn list_remove_at<T: Copy>(items: List<T>, index: int)
```

### `list_insert_at`

```punpun
fn list_insert_at<T: Copy>(items: List<T>, index: int, value: T)
```

### `chunk_ints`

```punpun
fn chunk_ints(items: List<int>, size: int) -> List<int>
```

### `dedupe_ints`

```punpun
fn dedupe_ints(items: List<int>) -> List<int>
```

### `dedupe_strings`

```punpun
fn dedupe_strings(items: List<str>) -> List<str>
```

### `filter_greater`

```punpun
fn filter_greater(items: List<int>, threshold: int) -> List<int>
```

### `map_scale`

```punpun
fn map_scale(items: List<int>, factor: int) -> List<int>
```

### `list_index_of_str`

```punpun
fn list_index_of_str(items: List<str>, value: str) -> int
```

### `list_contains_str`

```punpun
fn list_contains_str(items: List<str>, value: str) -> bool
```

## `stdlib/std/collections/queue.pp`

### `queue_push`

```punpun
fn queue_push<T: Copy>(items: List<T>, value: T)
```

### `queue_pop`

```punpun
fn queue_pop<T: Copy>(items: List<T>) -> T
```

### `queue_peek`

```punpun
fn queue_peek<T: Copy>(items: List<T>) -> T
```

### `queue_is_empty`

```punpun
fn queue_is_empty<T: Copy>(items: List<T>) -> bool
```

## `stdlib/std/collections/search.pp`

### `binary_search`

```punpun
fn binary_search(items: List<int>, value: int) -> int
```

### `lower_bound`

```punpun
fn lower_bound(items: List<int>, value: int) -> int
```

### `linear_search`

```punpun
fn linear_search(items: List<int>, value: int) -> int
```

### `contains_int`

```punpun
fn contains_int(items: List<int>, value: int) -> bool
```

### `count_int`

```punpun
fn count_int(items: List<int>, value: int) -> int
```

### `min_int_of`

```punpun
fn min_int_of(items: List<int>) -> int
```

### `max_int_of`

```punpun
fn max_int_of(items: List<int>) -> int
```

### `sum_ints`

```punpun
fn sum_ints(items: List<int>) -> int
```

## `stdlib/std/collections/set.pp`

### `set_new`

```punpun
fn set_new() -> Map<bool>
```

### `set_add`

```punpun
fn set_add(items: Map<bool>, value: str)
```

### `set_has`

```punpun
fn set_has(items: Map<bool>, value: str) -> bool
```

### `set_remove`

```punpun
fn set_remove(items: Map<bool>, value: str) -> bool
```

### `set_size`

```punpun
fn set_size(items: Map<bool>) -> int
```

### `set_values`

```punpun
fn set_values(items: Map<bool>) -> List<str>
```

### `set_union`

```punpun
fn set_union(left: Map<bool>, right: Map<bool>) -> Map<bool>
```

### `set_intersection`

```punpun
fn set_intersection(left: Map<bool>, right: Map<bool>) -> Map<bool>
```

### `set_difference`

```punpun
fn set_difference(left: Map<bool>, right: Map<bool>) -> Map<bool>
```

## `stdlib/std/collections/sorting.pp`

### `sort_ints`

```punpun
fn sort_ints(items: List<int>)
```

### `quicksort_ints`

```punpun
fn quicksort_ints(items: List<int>, low: int, high: int)
```

### `insertion_sort_ints`

```punpun
fn insertion_sort_ints(items: List<int>, low: int, high: int)
```

### `median_of_three`

```punpun
fn median_of_three(items: List<int>, low: int, high: int) -> int
```

### `sort_strings`

```punpun
fn sort_strings(items: List<str>)
```

### `is_sorted_ints`

```punpun
fn is_sorted_ints(items: List<int>) -> bool
```

### `reverse_ints`

```punpun
fn reverse_ints(items: List<int>)
```

## `stdlib/std/collections/stack.pp`

### `stack_push`

```punpun
fn stack_push<T: Copy>(items: List<T>, value: T)
```

### `stack_pop`

```punpun
fn stack_pop<T: Copy>(items: List<T>) -> T
```

### `stack_peek`

```punpun
fn stack_peek<T: Copy>(items: List<T>) -> T
```

### `stack_is_empty`

```punpun
fn stack_is_empty<T: Copy>(items: List<T>) -> bool
```

## `stdlib/std/data/base64.pp`

### `base64_alphabet`

```punpun
fn base64_alphabet() -> str
```

### `base64_encode`

```punpun
fn base64_encode(data: bytes) -> str
```

### `base64_value`

```punpun
fn base64_value(code: int) -> int
```

### `base64_decode`

```punpun
fn base64_decode(text_value: str) -> bytes
```

### `base64_encode_text`

```punpun
fn base64_encode_text(text_value: str) -> str
```

### `base64_decode_text`

```punpun
fn base64_decode_text(text_value: str) -> str
```

## `stdlib/std/data/binary.pp`

### `write_u16_le`

```punpun
fn write_u16_le(buffer: bytes, value: int)
```

### `write_u16_be`

```punpun
fn write_u16_be(buffer: bytes, value: int)
```

### `write_u32_le`

```punpun
fn write_u32_le(buffer: bytes, value: int)
```

### `write_u32_be`

```punpun
fn write_u32_be(buffer: bytes, value: int)
```

### `read_u16_le`

```punpun
fn read_u16_le(buffer: bytes, offset: int) -> int
```

### `read_u16_be`

```punpun
fn read_u16_be(buffer: bytes, offset: int) -> int
```

### `read_u32_le`

```punpun
fn read_u32_le(buffer: bytes, offset: int) -> int
```

### `read_u32_be`

```punpun
fn read_u32_be(buffer: bytes, offset: int) -> int
```

### `write_varint`

```punpun
fn write_varint(buffer: bytes, value: int)
```

### `read_varint`

```punpun
fn read_varint(buffer: bytes, offset: int) -> int
```

### `varint_size`

```punpun
fn varint_size(value: int) -> int
```

## `stdlib/std/data/checksum.pp`

### `crc32`

```punpun
fn crc32(data: bytes) -> int
```

### `fnv1a`

```punpun
fn fnv1a(data: bytes) -> int
```

### `mask32`

```punpun
fn mask32(value: int) -> int
```

### `fnv1a_text`

```punpun
fn fnv1a_text(value: str) -> int
```

### `adler32`

```punpun
fn adler32(data: bytes) -> int
```

### `checksum_text`

```punpun
fn checksum_text(value: str) -> int
```

## `stdlib/std/data/csv.pp`

### `csv_escape`

```punpun
fn csv_escape(field: str) -> str
```

### `csv_write_row`

```punpun
fn csv_write_row(fields: List<str>) -> str
```

### `csv_parse_row`

```punpun
fn csv_parse_row(line: str) -> List<str>
```

### `csv_write`

```punpun
fn csv_write(rows: List<str>) -> str
```

### `csv_field_count`

```punpun
fn csv_field_count(line: str) -> int
```

## `stdlib/std/data/hex.pp`

### `hex_digits`

```punpun
fn hex_digits() -> str
```

### `hex_encode`

```punpun
fn hex_encode(data: bytes) -> str
```

### `hex_decode`

```punpun
fn hex_decode(text_value: str) -> bytes
```

### `hex_value`

```punpun
fn hex_value(code: int) -> int
```

### `hex_of_int`

```punpun
fn hex_of_int(value: int) -> str
```

## `stdlib/std/data/ini.pp`

### `ini_parse`

```punpun
fn ini_parse(source: str) -> Map<str>
```

### `ini_get`

```punpun
fn ini_get(values: Map<str>, section: str, key: str) -> str
```

### `ini_has`

```punpun
fn ini_has(values: Map<str>, section: str, key: str) -> bool
```

### `ini_get_int`

```punpun
fn ini_get_int(values: Map<str>, section: str, key: str, fallback: int) -> int
```

### `ini_get_bool`

```punpun
fn ini_get_bool(values: Map<str>, section: str, key: str, fallback: bool) -> bool
```

## `stdlib/std/data/query.pp`

### `is_unreserved`

```punpun
fn is_unreserved(code: int) -> bool
```

### `url_encode`

```punpun
fn url_encode(value: str) -> str
```

### `url_decode`

```punpun
fn url_decode(value: str) -> str
```

### `hex_nibble`

```punpun
fn hex_nibble(code: int) -> int
```

### `query_encode`

```punpun
fn query_encode(values: Map<str>) -> str
```

### `query_decode`

```punpun
fn query_decode(query: str) -> Map<str>
```

## `stdlib/std/data/uuid.pp`

### `uuid4`

```punpun
fn uuid4() -> str
```

### `is_uuid`

```punpun
fn is_uuid(value: str) -> bool
```

### `uuid_nil`

```punpun
fn uuid_nil() -> str
```

## `stdlib/std/fs.pp`

### `fs_exists`

```punpun
fn fs_exists(path: String) -> bool
```

### `fs_read`

```punpun
fn fs_read(path: String) -> String
```

### `fs_write`

```punpun
fn fs_write(path: String, contents: String) -> void
```

### `fs_make_dir`

```punpun
fn fs_make_dir(path: String) -> bool
```

### `fs_remove`

```punpun
fn fs_remove(path: String) -> bool
```

### `fs_rename`

```punpun
fn fs_rename(source: String, destination: String) -> bool
```

### `fs_join`

```punpun
fn fs_join(left: String, right: String) -> String
```

## `stdlib/std/gui.pp`

### `gui_supported`

```punpun
fn gui_supported() -> bool
```

Cross-platform native GUI helpers. gui_available() and gui_message(title, message) are compiler builtins. On Windows they use Win32. Linux and other POSIX hosts use X11/XWayland when it is installed and a display is available; headless programs receive false.

### `gui_alert`

```punpun
fn gui_alert(message: String) -> bool
```

## `stdlib/std/io.pp`

### `io_read_line`

```punpun
fn io_read_line() -> String
```

## `stdlib/std/math.pp`

### `minimum`

```punpun
fn minimum(left: i64, right: i64) -> i64
```

### `maximum`

```punpun
fn maximum(left: i64, right: i64) -> i64
```

### `clamp`

```punpun
fn clamp(value: i64, lower: i64, upper: i64) -> i64
```

### `gcd`

```punpun
fn gcd(left: i64, right: i64) -> i64
```

### `factorial`

```punpun
fn factorial(n: i64) -> i64
```

### `integer_power`

```punpun
fn integer_power(base: i64, exponent: i64) -> i64
```

## `stdlib/std/math_ext/bits.pp`

### `bit_get`

```punpun
fn bit_get(value: int, index: int) -> bool
```

### `bit_set`

```punpun
fn bit_set(value: int, index: int) -> int
```

### `bit_clear`

```punpun
fn bit_clear(value: int, index: int) -> int
```

### `bit_toggle`

```punpun
fn bit_toggle(value: int, index: int) -> int
```

### `popcount`

```punpun
fn popcount(value: int) -> int
```

### `leading_zeros`

```punpun
fn leading_zeros(value: int) -> int
```

### `trailing_zeros`

```punpun
fn trailing_zeros(value: int) -> int
```

### `is_power_of_two`

```punpun
fn is_power_of_two(value: int) -> bool
```

### `next_power_of_two`

```punpun
fn next_power_of_two(value: int) -> int
```

### `to_binary`

```punpun
fn to_binary(value: int) -> str
```

### `from_binary`

```punpun
fn from_binary(digits: str) -> int
```

### `rotate_left`

```punpun
fn rotate_left(value: int, amount: int) -> int
```

## `stdlib/std/math_ext/constants.pp`

### `pi`

```punpun
fn pi() -> float
```

### `tau`

```punpun
fn tau() -> float
```

### `half_pi`

```punpun
fn half_pi() -> float
```

### `e`

```punpun
fn e() -> float
```

### `sqrt2`

```punpun
fn sqrt2() -> float
```

### `sqrt3`

```punpun
fn sqrt3() -> float
```

### `golden_ratio`

```punpun
fn golden_ratio() -> float
```

### `ln2`

```punpun
fn ln2() -> float
```

### `ln10`

```punpun
fn ln10() -> float
```

### `int_max`

```punpun
fn int_max() -> int
```

### `int_min`

```punpun
fn int_min() -> int
```

### `epsilon`

```punpun
fn epsilon() -> float
```

### `degrees_to_radians`

```punpun
fn degrees_to_radians(degrees: float) -> float
```

### `radians_to_degrees`

```punpun
fn radians_to_degrees(radians: float) -> float
```

## `stdlib/std/math_ext/floats.pp`

### `float_min`

```punpun
fn float_min(a: float, b: float) -> float
```

### `float_max`

```punpun
fn float_max(a: float, b: float) -> float
```

### `float_clamp`

```punpun
fn float_clamp(value: float, low: float, high: float) -> float
```

### `nearly_equal`

```punpun
fn nearly_equal(a: float, b: float, tolerance: float) -> bool
```

### `lerp`

```punpun
fn lerp(a: float, b: float, t: float) -> float
```

### `inverse_lerp`

```punpun
fn inverse_lerp(low: float, high: float, value: float) -> float
```

### `remap`

```punpun
fn remap(value: float, from_low: float, from_high: float,
```

### `smoothstep`

```punpun
fn smoothstep(low: float, high: float, value: float) -> float
```

### `round_to`

```punpun
fn round_to(value: float, places: int) -> float
```

### `truncate`

```punpun
fn truncate(value: float) -> float
```

### `float_sign`

```punpun
fn float_sign(value: float) -> float
```

## `stdlib/std/math_ext/integers.pp`

### `gcd`

```punpun
fn gcd(a: int, b: int) -> int
```

### `lcm`

```punpun
fn lcm(a: int, b: int) -> int
```

### `is_even`

```punpun
fn is_even(value: int) -> bool
```

### `is_odd`

```punpun
fn is_odd(value: int) -> bool
```

### `sign_of`

```punpun
fn sign_of(value: int) -> int
```

### `min_of`

```punpun
fn min_of(a: int, b: int) -> int
```

### `max_of`

```punpun
fn max_of(a: int, b: int) -> int
```

### `clamp`

```punpun
fn clamp(value: int, low: int, high: int) -> int
```

### `is_prime`

```punpun
fn is_prime(value: int) -> bool
```

### `next_prime`

```punpun
fn next_prime(value: int) -> int
```

### `primes_up_to`

```punpun
fn primes_up_to(limit: int) -> List<int>
```

### `factorial`

```punpun
fn factorial(value: int) -> int
```

### `pow_mod`

```punpun
fn pow_mod(base: int, exponent: int, modulus: int) -> int
```

### `int_pow`

```punpun
fn int_pow(base: int, exponent: int) -> int
```

### `int_sqrt`

```punpun
fn int_sqrt(value: int) -> int
```

### `digits_of`

```punpun
fn digits_of(value: int) -> List<int>
```

### `digit_sum`

```punpun
fn digit_sum(value: int) -> int
```

### `fibonacci`

```punpun
fn fibonacci(index: int) -> int
```

## `stdlib/std/math_ext/shuffling.pp`

### `shuffle_ints`

```punpun
fn shuffle_ints(items: List<int>)
```

### `shuffle_strings`

```punpun
fn shuffle_strings(items: List<str>)
```

### `choice_int`

```punpun
fn choice_int(items: List<int>) -> int
```

### `choice_str`

```punpun
fn choice_str(items: List<str>) -> str
```

### `sample_ints`

```punpun
fn sample_ints(items: List<int>, count: int) -> List<int>
```

### `random_range`

```punpun
fn random_range(low: float, high: float) -> float
```

### `random_bool`

```punpun
fn random_bool(probability: float) -> bool
```

### `random_gaussian`

```punpun
fn random_gaussian(mean_value: float, deviation: float) -> float
```

### `random_digits`

```punpun
fn random_digits(count: int) -> str
```

## `stdlib/std/math_ext/statistics.pp`

### `mean`

```punpun
fn mean(values: List<float>) -> float
```

### `sum_floats`

```punpun
fn sum_floats(values: List<float>) -> float
```

### `min_float_of`

```punpun
fn min_float_of(values: List<float>) -> float
```

### `max_float_of`

```punpun
fn max_float_of(values: List<float>) -> float
```

### `variance`

```punpun
fn variance(values: List<float>) -> float
```

### `sample_variance`

```punpun
fn sample_variance(values: List<float>) -> float
```

### `standard_deviation`

```punpun
fn standard_deviation(values: List<float>) -> float
```

### `median`

```punpun
fn median(values: List<float>) -> float
```

### `percentile`

```punpun
fn percentile(values: List<float>, fraction: float) -> float
```

### `range_of`

```punpun
fn range_of(values: List<float>) -> float
```

### `sort_floats_copy`

```punpun
fn sort_floats_copy(values: List<float>) -> List<float>
```

## `stdlib/std/math_ext/vector.pp`

### `Vec2`

```punpun
struct Vec2
```

### `Vec3`

```punpun
struct Vec3
```

### `vec2`

```punpun
fn vec2(x: float, y: float) -> Vec2
```

### `vec2_zero`

```punpun
fn vec2_zero() -> Vec2
```

### `vec2_add`

```punpun
fn vec2_add(a: Vec2, b: Vec2) -> Vec2
```

### `vec2_sub`

```punpun
fn vec2_sub(a: Vec2, b: Vec2) -> Vec2
```

### `vec2_scale`

```punpun
fn vec2_scale(a: Vec2, factor: float) -> Vec2
```

### `vec2_dot`

```punpun
fn vec2_dot(a: Vec2, b: Vec2) -> float
```

### `vec2_length`

```punpun
fn vec2_length(a: Vec2) -> float
```

### `vec2_length_squared`

```punpun
fn vec2_length_squared(a: Vec2) -> float
```

### `vec2_normalize`

```punpun
fn vec2_normalize(a: Vec2) -> Vec2
```

### `vec2_distance`

```punpun
fn vec2_distance(a: Vec2, b: Vec2) -> float
```

### `vec2_angle`

```punpun
fn vec2_angle(a: Vec2) -> float
```

### `vec2_lerp`

```punpun
fn vec2_lerp(a: Vec2, b: Vec2, t: float) -> Vec2
```

### `vec2_rotate`

```punpun
fn vec2_rotate(a: Vec2, radians: float) -> Vec2
```

### `vec2_to_text`

```punpun
fn vec2_to_text(a: Vec2) -> str
```

### `vec3`

```punpun
fn vec3(x: float, y: float, z: float) -> Vec3
```

### `vec3_zero`

```punpun
fn vec3_zero() -> Vec3
```

### `vec3_add`

```punpun
fn vec3_add(a: Vec3, b: Vec3) -> Vec3
```

### `vec3_sub`

```punpun
fn vec3_sub(a: Vec3, b: Vec3) -> Vec3
```

### `vec3_scale`

```punpun
fn vec3_scale(a: Vec3, factor: float) -> Vec3
```

### `vec3_dot`

```punpun
fn vec3_dot(a: Vec3, b: Vec3) -> float
```

### `vec3_cross`

```punpun
fn vec3_cross(a: Vec3, b: Vec3) -> Vec3
```

### `vec3_length`

```punpun
fn vec3_length(a: Vec3) -> float
```

### `vec3_normalize`

```punpun
fn vec3_normalize(a: Vec3) -> Vec3
```

### `vec3_distance`

```punpun
fn vec3_distance(a: Vec3, b: Vec3) -> float
```

### `vec3_to_text`

```punpun
fn vec3_to_text(a: Vec3) -> str
```

## `stdlib/std/net/https.pp`

### `https_get`

```punpun
fn https_get(url: String) -> String
```

Verified HTTPS helpers. The primitive operations are compiler builtins backed by the system libcurl runtime. Certificate and hostname verification are always enabled, redirects remain HTTPS-only, and plain HTTP URLs are rejected.

### `https_get_with_headers`

```punpun
fn https_get_with_headers(url: String, headers: String) -> String
```

### `https_post`

```punpun
fn https_post(url: String, body: String, content_type: String) -> String
```

### `https_put`

```punpun
fn https_put(url: String, body: String, content_type: String) -> String
```

### `https_delete`

```punpun
fn https_delete(url: String) -> String
```

### `https_head`

```punpun
fn https_head(url: String) -> String
```

### `https_ok`

```punpun
fn https_ok() -> bool
```

### `https_get_async`

```punpun
async fn https_get_async(url: String) -> String
```

### `https_post_async`

```punpun
async fn https_post_async(url: String, body: String, content_type: String) -> String
```

## `stdlib/std/nums.pp`

### `nums_copy`

```punpun
fn nums_copy(values: nums) -> nums
```

### `nums_contains`

```punpun
fn nums_contains(values: nums, needle: i64) -> bool
```

### `nums_index_of`

```punpun
fn nums_index_of(values: nums, needle: i64) -> i64
```

### `nums_count`

```punpun
fn nums_count(values: nums, needle: i64) -> i64
```

### `nums_reverse`

```punpun
fn nums_reverse(values: nums) -> nums
```

### `nums_equal`

```punpun
fn nums_equal(left: nums, right: nums) -> bool
```

## `stdlib/std/option.pp`

### `option_is_some`

```punpun
fn option_is_some<T>(value: Option<T>) -> bool
```

Generic Option helpers. Option<T> itself is a prelude algebraic enum.

### `option_is_none`

```punpun
fn option_is_none<T>(value: Option<T>) -> bool
```

### `option_unwrap_or`

```punpun
fn option_unwrap_or<T: Copy>(value: Option<T>, fallback: T) -> T
```

## `stdlib/std/result.pp`

### `result_is_ok`

```punpun
fn result_is_ok<T, E>(value: Result<T, E>) -> bool
```

Generic Result helpers. Result<T,E> itself is a prelude algebraic enum.

### `result_is_error`

```punpun
fn result_is_error<T, E>(value: Result<T, E>) -> bool
```

### `result_unwrap_or`

```punpun
fn result_unwrap_or<T: Copy, E>(value: Result<T, E>, fallback: T) -> T
```

## `stdlib/std/stats.pp`

### `stats_sum`

```punpun
fn stats_sum(values: nums) -> i64
```

### `stats_mean`

```punpun
fn stats_mean(values: nums) -> f64
```

### `stats_min`

```punpun
fn stats_min(values: nums) -> i64
```

### `stats_max`

```punpun
fn stats_max(values: nums) -> i64
```

### `stats_sorted`

```punpun
fn stats_sorted(values: nums) -> nums
```

## `stdlib/std/system.pp`

### `system_platform`

```punpun
fn system_platform() -> String
```

### `system_current_dir`

```punpun
fn system_current_dir() -> String
```

### `system_env_has`

```punpun
fn system_env_has(name: String) -> bool
```

### `system_env_or`

```punpun
fn system_env_or(name: String, fallback: String) -> String
```

## `stdlib/std/system_ext/cli.pp`

### `Args`

```punpun
struct Args
```

### `parse_args`

```punpun
fn parse_args() -> Args
```

### `parse_arg_list`

```punpun
fn parse_arg_list(items: List<str>) -> Args
```

### `has_flag`

```punpun
fn has_flag(parsed: Args, name: str) -> bool
```

### `get_option`

```punpun
fn get_option(parsed: Args, name: str, fallback: str) -> str
```

### `get_option_int`

```punpun
fn get_option_int(parsed: Args, name: str, fallback: int) -> int
```

### `positional_count`

```punpun
fn positional_count(parsed: Args) -> int
```

### `positional_at`

```punpun
fn positional_at(parsed: Args, index: int, fallback: str) -> str
```

## `stdlib/std/system_ext/console.pp`

### `escape`

```punpun
fn escape(code: str) -> str
```

### `reset`

```punpun
fn reset() -> str
```

### `bold`

```punpun
fn bold(value: str) -> str
```

### `dim`

```punpun
fn dim(value: str) -> str
```

### `italic`

```punpun
fn italic(value: str) -> str
```

### `underline`

```punpun
fn underline(value: str) -> str
```

### `red`

```punpun
fn red(value: str) -> str
```

### `green`

```punpun
fn green(value: str) -> str
```

### `yellow`

```punpun
fn yellow(value: str) -> str
```

### `blue`

```punpun
fn blue(value: str) -> str
```

### `magenta`

```punpun
fn magenta(value: str) -> str
```

### `cyan`

```punpun
fn cyan(value: str) -> str
```

### `gray`

```punpun
fn gray(value: str) -> str
```

### `on_red`

```punpun
fn on_red(value: str) -> str
```

### `on_green`

```punpun
fn on_green(value: str) -> str
```

### `rgb`

```punpun
fn rgb(value: str, r: int, g: int, b: int) -> str
```

### `clear_screen`

```punpun
fn clear_screen()
```

### `move_cursor`

```punpun
fn move_cursor(row: int, column: int)
```

### `hide_cursor`

```punpun
fn hide_cursor()
```

### `show_cursor`

```punpun
fn show_cursor()
```

### `progress_bar`

```punpun
fn progress_bar(fraction: float, width: int) -> str
```

## `stdlib/std/system_ext/files.pp`

### `read_lines`

```punpun
fn read_lines(path: str) -> List<str>
```

### `write_lines`

```punpun
fn write_lines(path: str, lines: List<str>)
```

### `append_line`

```punpun
fn append_line(path: str, line: str)
```

### `count_lines`

```punpun
fn count_lines(path: str) -> int
```

### `read_lines_non_empty`

```punpun
fn read_lines_non_empty(path: str) -> List<str>
```

### `copy_file`

```punpun
fn copy_file(source: str, destination: str)
```

### `ensure_parent_dir`

```punpun
fn ensure_parent_dir(path: str) -> bool
```

### `write_text_atomic`

```punpun
fn write_text_atomic(path: str, content: str)
```

### `read_text_or`

```punpun
fn read_text_or(path: str, fallback: str) -> str
```

## `stdlib/std/system_ext/log.pp`

### `level_debug`

```punpun
fn level_debug() -> int
```

### `level_info`

```punpun
fn level_info() -> int
```

### `level_warn`

```punpun
fn level_warn() -> int
```

### `level_error`

```punpun
fn level_error() -> int
```

### `level_name`

```punpun
fn level_name(level: int) -> str
```

### `log_at`

```punpun
fn log_at(threshold: int, level: int, message: str)
```

### `debug`

```punpun
fn debug(threshold: int, message: str)
```

### `info`

```punpun
fn info(threshold: int, message: str)
```

### `warn`

```punpun
fn warn(threshold: int, message: str)
```

### `error`

```punpun
fn error(threshold: int, message: str)
```

### `log_stamped`

```punpun
fn log_stamped(threshold: int, level: int, message: str)
```

### `format_two`

```punpun
fn format_two(value: int) -> str
```

### `log_to_file`

```punpun
fn log_to_file(path: str, level: int, message: str)
```

## `stdlib/std/system_ext/paths.pp`

### `join_all`

```punpun
fn join_all(parts: List<str>) -> str
```

### `without_extension`

```punpun
fn without_extension(path: str) -> str
```

### `with_extension`

```punpun
fn with_extension(path: str, extension: str) -> str
```

### `is_absolute`

```punpun
fn is_absolute(path: str) -> bool
```

### `normalize_separators`

```punpun
fn normalize_separators(path: str) -> str
```

### `split_path`

```punpun
fn split_path(path: str) -> List<str>
```

### `has_extension`

```punpun
fn has_extension(path: str, extension: str) -> bool
```

### `walk_files`

```punpun
fn walk_files(root: str, max_depth: int) -> List<str>
```

### `walk_into`

```punpun
fn walk_into(directory: str, depth_left: int, found: List<str>)
```

## `stdlib/std/system_ext/timer.pp`

### `Stopwatch`

```punpun
struct Stopwatch
```

### `stopwatch_new`

```punpun
fn stopwatch_new() -> Stopwatch
```

### `stopwatch_start`

```punpun
fn stopwatch_start(watch: Stopwatch) -> Stopwatch
```

### `stopwatch_stop`

```punpun
fn stopwatch_stop(watch: Stopwatch) -> Stopwatch
```

### `stopwatch_elapsed`

```punpun
fn stopwatch_elapsed(watch: Stopwatch) -> int
```

### `stopwatch_reset`

```punpun
fn stopwatch_reset() -> Stopwatch
```

### `time_rounds`

```punpun
fn time_rounds(rounds: int) -> List<int>
```

### `elapsed_since`

```punpun
fn elapsed_since(start: int) -> int
```

### `format_elapsed`

```punpun
fn format_elapsed(milliseconds: int) -> str
```

## `stdlib/std/testing.pp`

### `expect_int`

```punpun
fn expect_int(actual: i64, expected: i64) -> void
```

### `expect_bool`

```punpun
fn expect_bool(actual: bool, expected: bool) -> void
```

### `expect_str`

```punpun
fn expect_str(actual: String, expected: String) -> void
```

## `stdlib/std/text.pp`

### `text_starts_with`

```punpun
fn text_starts_with(value: String, prefix: String) -> bool
```

### `text_ends_with`

```punpun
fn text_ends_with(value: String, suffix: String) -> bool
```

### `text_trim`

```punpun
fn text_trim(value: String) -> String
```

### `text_repeat`

```punpun
fn text_repeat(value: String, count: i64) -> String
```

### `text_is_utf8`

```punpun
fn text_is_utf8(value: String) -> bool
```

### `text_codepoints`

```punpun
fn text_codepoints(value: String) -> i64
```

## `stdlib/std/text/casing.pp`

### `to_snake_case`

```punpun
fn to_snake_case(value: str) -> str
```

### `to_kebab_case`

```punpun
fn to_kebab_case(value: str) -> str
```

### `to_camel_case`

```punpun
fn to_camel_case(value: str) -> str
```

### `to_pascal_case`

```punpun
fn to_pascal_case(value: str) -> str
```

### `to_screaming_snake_case`

```punpun
fn to_screaming_snake_case(value: str) -> str
```

## `stdlib/std/text/distance.pp`

### `levenshtein`

```punpun
fn levenshtein(a: str, b: str) -> int
```

### `similarity`

```punpun
fn similarity(a: str, b: str) -> float
```

### `hamming`

```punpun
fn hamming(a: str, b: str) -> int
```

### `common_prefix`

```punpun
fn common_prefix(a: str, b: str) -> str
```

### `min_len`

```punpun
fn min_len(a: str, b: str) -> int
```

### `closest_match`

```punpun
fn closest_match(value: str, options: List<str>) -> str
```

## `stdlib/std/text/format.pp`

### `format_int`

```punpun
fn format_int(value: int, width: int) -> str
```

### `format_zero_padded`

```punpun
fn format_zero_padded(value: int, width: int) -> str
```

### `format_fixed`

```punpun
fn format_fixed(value: float, places: int) -> str
```

### `format_percent`

```punpun
fn format_percent(value: float, places: int) -> str
```

### `format_bytes`

```punpun
fn format_bytes(count: int) -> str
```

### `format_duration`

```punpun
fn format_duration(milliseconds: int) -> str
```

### `repeat_to_width`

```punpun
fn repeat_to_width(fill: str, width: int) -> str
```

## `stdlib/std/text/strings.pp`

### `is_empty`

```punpun
fn is_empty(text_value: str) -> bool
```

### `is_blank`

```punpun
fn is_blank(text_value: str) -> bool
```

### `char_code`

```punpun
fn char_code(text_value: str, index: int) -> int
```

### `substring`

```punpun
fn substring(text_value: str, start: int, count: int) -> str
```

### `left`

```punpun
fn left(text_value: str, count: int) -> str
```

### `right`

```punpun
fn right(text_value: str, count: int) -> str
```

### `reverse`

```punpun
fn reverse(text_value: str) -> str
```

### `count_occurrences`

```punpun
fn count_occurrences(text_value: str, part: str) -> int
```

### `trim_start`

```punpun
fn trim_start(text_value: str) -> str
```

### `trim_end`

```punpun
fn trim_end(text_value: str) -> str
```

### `is_space`

```punpun
fn is_space(code: int) -> bool
```

### `is_digit`

```punpun
fn is_digit(code: int) -> bool
```

### `is_upper`

```punpun
fn is_upper(code: int) -> bool
```

### `is_lower`

```punpun
fn is_lower(code: int) -> bool
```

### `is_alpha`

```punpun
fn is_alpha(code: int) -> bool
```

### `is_alnum`

```punpun
fn is_alnum(code: int) -> bool
```

### `is_hex_digit`

```punpun
fn is_hex_digit(code: int) -> bool
```

### `capitalize`

```punpun
fn capitalize(text_value: str) -> str
```

### `title_case`

```punpun
fn title_case(text_value: str) -> str
```

### `starts_with_any`

```punpun
fn starts_with_any(text_value: str, options: List<str>) -> bool
```

### `split_lines`

```punpun
fn split_lines(text_value: str) -> List<str>
```

### `split_whitespace`

```punpun
fn split_whitespace(text_value: str) -> List<str>
```

### `strip_prefix`

```punpun
fn strip_prefix(text_value: str, prefix: str) -> str
```

### `strip_suffix`

```punpun
fn strip_suffix(text_value: str, suffix: str) -> str
```

### `center`

```punpun
fn center(text_value: str, width: int, fill: str) -> str
```

### `equals_ignore_case`

```punpun
fn equals_ignore_case(left_value: str, right_value: str) -> bool
```

## `stdlib/std/text/wrap.pp`

### `wrap_text`

```punpun
fn wrap_text(value: str, width: int) -> List<str>
```

### `wrap_to_text`

```punpun
fn wrap_to_text(value: str, width: int) -> str
```

### `indent`

```punpun
fn indent(value: str, prefix: str) -> str
```

### `dedent`

```punpun
fn dedent(value: str) -> str
```

### `truncate_text`

```punpun
fn truncate_text(value: str, width: int, ellipsis: str) -> str
```

## `stdlib/std/time.pp`

### `elapsed_ms`

```punpun
fn elapsed_ms(start: i64) -> i64
```

### `deadline_reached`

```punpun
fn deadline_reached(deadline: i64) -> bool
```

### `deadline_after_ms`

```punpun
fn deadline_after_ms(duration: i64) -> i64
```

### `delay_ms`

```punpun
fn delay_ms(duration: i64) -> void
```
