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

## `packages/gui/src/main.pp`

### `pp_gui_available`

```punpun
extern native fn pp_gui_available() -> i64;
```

### `pp_gui_message`

```punpun
extern native fn pp_gui_message(title: String, message: String) -> i64;
```

### `gui_available`

```punpun
fn gui_available() -> bool
```

### `gui_message`

```punpun
fn gui_message(title: String, message: String) -> bool
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

### `curl_slist`

```punpun
struct curl_slist;
```

### `curl_slist`

```punpun
struct curl_slist *list=NULL;
```

### `curl_slist`

```punpun
struct curl_slist *next=p_slist_append(list,line);
```

### `curl_slist`

```punpun
struct curl_slist *header_list=pp_req_headers(headers);
```

### `pp_requests_request`

```punpun
extern native fn pp_requests_request(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow: i64) -> String;
```

### `pp_requests_status`

```punpun
extern native fn pp_requests_status() -> i64;
```

### `pp_requests_error`

```punpun
extern native fn pp_requests_error() -> String;
```

### `pp_requests_available`

```punpun
extern native fn pp_requests_available() -> i64;
```

### `HttpResponse`

```punpun
object HttpResponse
```

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
fn requests_request(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow_redirects: bool) -> HttpResponse
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
async fn requests_request_async(method: String, url: String, body: String, headers: String, timeout_ms: i64, follow_redirects: bool) -> HttpResponse
```

Async networking helpers. They reuse the same requests implementation inside native PunPun tasks, so callers can compose network work with task groups and cancellation without introducing a second HTTP stack.

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
