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
fn file_read(path_value: String) -> String
```

### `file_write`

```punpun
fn file_write(path_value: String, contents: String)
```

### `file_present`

```punpun
fn file_present(path_value: String) -> bool
```

### `working_directory`

```punpun
fn working_directory() -> String
```

### `file_copy`

```punpun
fn file_copy(source:String,destination:String)->void
```

### `tree_copy`

```punpun
fn tree_copy(source:String,destination:String)->void
```

### `tree_remove`

```punpun
fn tree_remove(target:String)->bool
```

### `normalize_path`

```punpun
fn normalize_path(value:String)->String
```

## `packages/json/src/main.pp`

### `json_valid`

```punpun
fn json_valid(text: String) -> bool
```

PPX json now delegates to the full PunPun standard-library implementation. The old 0.1 convenience functions remain source-compatible.

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

### `json_decode`

```punpun
fn json_decode(text:String)->JsonParseResult
```

### `json_encode`

```punpun
fn json_encode(value:JsonValue)->String
```

### `json_encode_pretty`

```punpun
fn json_encode_pretty(value:JsonValue,indent:int)->String
```

## `packages/logging/src/main.pp`

### `log_trace`

```punpun
fn log_trace(message: String)
```

Compatibility helpers plus access to the real Logger type in std.logging.

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

### `log_named`

```punpun
fn log_named(name:String,threshold:int)->Logger
```

### `log_file`

```punpun
fn log_file(name:String,threshold:int,path_value:String)->Logger
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

## `stdlib/std/archive/zip.pp`

### `ZipEntry`

```punpun
struct ZipEntry
```

Interoperable ZIP archives using method 0 (stored/no compression). The format machinery is deliberately implemented in PunPun.  Pair this with std.compress when an application wants to compress payloads before archiving.

### `ZipReadResult`

```punpun
struct ZipReadResult
```

### `ok`

```punpun
public fn ok() -> bool
```

### `get`

```punpun
public fn get(name: str) -> bytes
```

### `has`

```punpun
public fn has(name: str) -> bool
```

### `_zip_append`

```punpun
fn _zip_append(out: bytes, data: bytes) -> void
```

### `_zip_need`

```punpun
fn _zip_need(data: bytes, offset: int, amount: int) -> bool
```

### `zip_write`

```punpun
fn zip_write(entries: List<ZipEntry>) -> bytes
```

### `zip_read`

```punpun
fn zip_read(data: bytes) -> ZipReadResult
```

### `zip_write_file`

```punpun
fn zip_write_file(path: str, entries: List<ZipEntry>) -> void
```

### `zip_read_file`

```punpun
fn zip_read_file(path: str) -> ZipReadResult
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

## `stdlib/std/collections/deque.pp`

### `Deque`

```punpun
object Deque<T: Copy>
```

Amortized O(1) FIFO deque using a List plus a moving head. Front removals do not shift the whole list; occasional compaction bounds retained dead slots.

### `size`

```punpun
public fn size()->int
```

### `empty`

```punpun
public fn empty()->bool
```

### `push_back`

```punpun
public fn push_back(value:T)->void
```

### `front`

```punpun
public fn front()->T
```

### `back`

```punpun
public fn back()->T
```

### `pop_front`

```punpun
public fn pop_front()->T
```

### `pop_back`

```punpun
public fn pop_back()->T
```

### `clear`

```punpun
public fn clear()->void
```

### `compact`

```punpun
public fn compact()->void
```

## `stdlib/std/collections/functional.pp`

### `list_map`

```punpun
fn list_map<T: Copy, U: Copy>(items: List<T>, transform: fn(T) -> U) -> List<U>
```

Higher-order collection operations unlocked by capturing function values. These are deliberately library code: map/filter/fold do not need compiler magic.

### `list_filter`

```punpun
fn list_filter<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> List<T>
```

### `list_fold`

```punpun
fn list_fold<T: Copy, U: Copy>(items: List<T>, initial: U, combine: fn(U,T) -> U) -> U
```

### `list_any`

```punpun
fn list_any<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> bool
```

### `list_all`

```punpun
fn list_all<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> bool
```

### `list_count_if`

```punpun
fn list_count_if<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> int
```

### `list_find_index`

```punpun
fn list_find_index<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> int
```

### `list_for_each`

```punpun
fn list_for_each<T: Copy>(items: List<T>, action: fn(T) -> void) -> void
```

### `list_take`

```punpun
fn list_take<T: Copy>(items: List<T>, count: int) -> List<T>
```

### `list_drop`

```punpun
fn list_drop<T: Copy>(items: List<T>, count: int) -> List<T>
```

### `Indexed`

```punpun
struct Indexed<T>
```

### `list_enumerate`

```punpun
fn list_enumerate<T: Copy>(items: List<T>) -> List<Indexed<T>>
```

### `Pair`

```punpun
struct Pair<T,U>
```

### `list_zip`

```punpun
fn list_zip<T: Copy,U: Copy>(left:List<T>,right:List<U>)->List<Pair<T,U>>
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

### `sort_by`

```punpun
fn sort_by(items: List<int>, before: fn(int, int) -> bool)
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

## `stdlib/std/compress/lzss.pp`

### `_lz_push_u32`

```punpun
fn _lz_push_u32(out: bytes, value: int) -> void
```

LZSS compression implemented entirely in PunPun. Stream format: "PPLZ" + 32-bit original size + groups of one flag byte and up to eight tokens. Flag 1 = literal byte; flag 0 = two-byte (offset,length) backreference. Window 4095 bytes, match length 3..18.

### `_lz_read_u32`

```punpun
fn _lz_read_u32(data: bytes, at: int) -> int
```

### `_LzMatch`

```punpun
struct _LzMatch
```

### `_lz_best`

```punpun
fn _lz_best(data: bytes, pos: int) -> _LzMatch
```

### `lzss_compress`

```punpun
fn lzss_compress(data: bytes) -> bytes
```

### `lzss_decompress`

```punpun
fn lzss_decompress(data: bytes) -> bytes
```

### `lzss_compress_text`

```punpun
fn lzss_compress_text(value: str) -> bytes
```

### `lzss_decompress_text`

```punpun
fn lzss_decompress_text(data: bytes) -> str
```

## `stdlib/std/compress/rle.pp`

### `rle_compress`

```punpun
fn rle_compress(data: bytes) -> bytes
```

Byte run-length encoding. Excellent for repetitive masks/images, intentionally simple and deterministic. Pairs are (count,value), count 1..255.

### `rle_decompress`

```punpun
fn rle_decompress(data: bytes) -> bytes
```

## `stdlib/std/config.pp`

### `Config`

```punpun
object Config
```

Layered application configuration. Values are typed JsonValue entries so a config can hold strings, numbers, booleans and arrays without stringly-typed conversions scattered across the application.

### `has`

```punpun
public fn has(key:str)->bool
```

### `set`

```punpun
public fn set(key:str,value:JsonValue)->void
```

### `get`

```punpun
public fn get(key:str)->JsonValue
```

### `string`

```punpun
public fn string(key:str,fallback:str)->str
```

### `integer`

```punpun
public fn integer(key:str,fallback:int)->int
```

### `boolean`

```punpun
public fn boolean(key:str,fallback:bool)->bool
```

### `overlay`

```punpun
public fn overlay(other:Map<JsonValue>)->void
```

### `env_string`

```punpun
public fn env_string(key:str,env_name:str)->void
```

### `env_int`

```punpun
public fn env_int(key:str,env_name:str)->void
```

### `env_bool`

```punpun
public fn env_bool(key:str,env_name:str)->void
```

### `config_from_toml`

```punpun
fn config_from_toml(source:str)->Config
```

### `config_load_toml`

```punpun
fn config_load_toml(path_value:str)->Config
```

## `stdlib/std/crypto/sha256.pp`

### `_u32`

```punpun
fn _u32(value: int) -> int
```

SHA-256 implemented in PunPun. Arithmetic is explicitly reduced to 32 bits so checked signed-int overflow never becomes part of the algorithm.

### `_rotr32`

```punpun
fn _rotr32(value: int, amount: int) -> int
```

### `_shr32`

```punpun
fn _shr32(value: int, amount: int) -> int
```

### `_sha_ch`

```punpun
fn _sha_ch(x: int, y: int, z: int) -> int
```

### `_sha_maj`

```punpun
fn _sha_maj(x: int, y: int, z: int) -> int
```

### `_sha_big0`

```punpun
fn _sha_big0(x: int) -> int
```

### `_sha_big1`

```punpun
fn _sha_big1(x: int) -> int
```

### `_sha_small0`

```punpun
fn _sha_small0(x: int) -> int
```

### `_sha_small1`

```punpun
fn _sha_small1(x: int) -> int
```

### `_sha_constants`

```punpun
fn _sha_constants() -> List<int>
```

### `_sha_push_u32`

```punpun
fn _sha_push_u32(out: bytes, value: int) -> void
```

### `sha256`

```punpun
fn sha256(data: bytes) -> bytes
```

### `sha256_text`

```punpun
fn sha256_text(value: str) -> bytes
```

### `sha256_hex`

```punpun
fn sha256_hex(data: bytes) -> str
```

### `sha256_text_hex`

```punpun
fn sha256_text_hex(value: str) -> str
```

### `hmac_sha256`

```punpun
fn hmac_sha256(key: bytes, message: bytes) -> bytes
```

### `hmac_sha256_hex`

```punpun
fn hmac_sha256_hex(key: bytes, message: bytes) -> str
```

### `hmac_sha256_text_hex`

```punpun
fn hmac_sha256_text_hex(key: str, message: str) -> str
```

### `constant_time_equal`

```punpun
fn constant_time_equal(left: bytes, right: bytes) -> bool
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

## `stdlib/std/data/json.pp`

### `json_null_kind`

```punpun
fn json_null_kind() -> int
```

Full JSON values, parsing, serialization and pretty-printing in PunPun. The runtime supplies only strings, bytes, List and Map. JSON semantics live here.

### `json_bool_kind`

```punpun
fn json_bool_kind() -> int
```

### `json_number_kind`

```punpun
fn json_number_kind() -> int
```

### `json_string_kind`

```punpun
fn json_string_kind() -> int
```

### `json_array_kind`

```punpun
fn json_array_kind() -> int
```

### `json_object_kind`

```punpun
fn json_object_kind() -> int
```

### `JsonValue`

```punpun
struct JsonValue
```

### `is_null`

```punpun
public fn is_null() -> bool
```

### `is_bool`

```punpun
public fn is_bool() -> bool
```

### `is_number`

```punpun
public fn is_number() -> bool
```

### `is_string`

```punpun
public fn is_string() -> bool
```

### `is_array`

```punpun
public fn is_array() -> bool
```

### `is_object`

```punpun
public fn is_object() -> bool
```

### `bool_or`

```punpun
public fn bool_or(fallback: bool) -> bool
```

### `number_or`

```punpun
public fn number_or(fallback: float) -> float
```

### `int_or`

```punpun
public fn int_or(fallback: int) -> int
```

### `string_or`

```punpun
public fn string_or(fallback: str) -> str
```

### `size`

```punpun
public fn size() -> int
```

### `at`

```punpun
public fn at(index: int) -> JsonValue
```

### `has`

```punpun
public fn has(key: str) -> bool
```

### `get`

```punpun
public fn get(key: str) -> JsonValue
```

### `get_string`

```punpun
public fn get_string(key: str, fallback: str) -> str
```

### `get_int`

```punpun
public fn get_int(key: str, fallback: int) -> int
```

### `get_bool`

```punpun
public fn get_bool(key: str, fallback: bool) -> bool
```

### `push`

```punpun
public fn push(value: JsonValue) -> JsonValue
```

### `put`

```punpun
public fn put(key: str, value: JsonValue) -> JsonValue
```

### `encode`

```punpun
public fn encode() -> str
```

### `pretty`

```punpun
public fn pretty(indent: int) -> str
```

### `json_null`

```punpun
fn json_null() -> JsonValue
```

### `json_bool`

```punpun
fn json_bool(value: bool) -> JsonValue
```

### `json_number`

```punpun
fn json_number(value: float) -> JsonValue
```

### `json_int`

```punpun
fn json_int(value: int) -> JsonValue
```

### `json_string`

```punpun
fn json_string(value: str) -> JsonValue
```

### `json_array`

```punpun
fn json_array() -> JsonValue
```

### `json_object`

```punpun
fn json_object() -> JsonValue
```

### `JsonParseResult`

```punpun
struct JsonParseResult
```

### `ok`

```punpun
public fn ok() -> bool
```

### `_JsonParser`

```punpun
object _JsonParser
```

### `fail`

```punpun
public fn fail(message: str) -> void
```

### `skip_space`

```punpun
public fn skip_space() -> void
```

### `take`

```punpun
public fn take(expected: int) -> bool
```

### `parse_value`

```punpun
public fn parse_value(depth: int) -> JsonValue
```

### `parse_literal`

```punpun
public fn parse_literal(word: str, value: JsonValue) -> JsonValue
```

### `parse_number`

```punpun
public fn parse_number() -> JsonValue
```

### `hex_digit`

```punpun
public fn hex_digit(c: int) -> int
```

### `unicode4`

```punpun
public fn unicode4() -> int
```

### `utf8`

```punpun
public fn utf8(codepoint: int) -> str
```

### `parse_string`

```punpun
public fn parse_string() -> str
```

### `parse_array`

```punpun
public fn parse_array(depth: int) -> JsonValue
```

### `parse_object`

```punpun
public fn parse_object(depth: int) -> JsonValue
```

### `json_parse`

```punpun
fn json_parse(source: str) -> JsonParseResult
```

### `json_parse_or_panic`

```punpun
fn json_parse_or_panic(source: str) -> JsonValue
```

### `_json_escape`

```punpun
fn _json_escape(value: str) -> str
```

### `_json_number`

```punpun
fn _json_number(value: float) -> str
```

### `json_stringify`

```punpun
fn json_stringify(value: JsonValue) -> str
```

### `_json_indent`

```punpun
fn _json_indent(depth: int, width: int) -> str
```

### `_json_pretty`

```punpun
fn _json_pretty(value: JsonValue, width: int, depth: int) -> str
```

### `json_pretty`

```punpun
fn json_pretty(value: JsonValue, indent: int) -> str
```

### `json_equal`

```punpun
fn json_equal(left: JsonValue, right: JsonValue) -> bool
```

## `stdlib/std/data/mime.pp`

### `mime_type`

```punpun
fn mime_type(path_value:str)->str
```

Common MIME inference by file extension. Unknown files intentionally use the safe generic binary type instead of guessing text.

### `mime_is_text`

```punpun
fn mime_is_text(value:str)->bool
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

## `stdlib/std/data/toml.pp`

### `TomlParseResult`

```punpun
struct TomlParseResult
```

Practical TOML configuration parser written in PunPun. Values reuse JsonValue because TOML's scalar/array/table data model overlaps JSON well. Keys are stored as dotted paths; this keeps lookup cheap and avoids hiding a second tree representation behind native code.

### `ok`

```punpun
public fn ok() -> bool
```

### `has`

```punpun
public fn has(path: str) -> bool
```

### `get`

```punpun
public fn get(path: str) -> JsonValue
```

### `get_string`

```punpun
public fn get_string(path: str, fallback: str) -> str
```

### `get_int`

```punpun
public fn get_int(path: str, fallback: int) -> int
```

### `get_bool`

```punpun
public fn get_bool(path: str, fallback: bool) -> bool
```

### `_toml_strip_comment`

```punpun
fn _toml_strip_comment(line: str) -> str
```

### `_toml_unescape_string`

```punpun
fn _toml_unescape_string(source: str) -> str
```

### `_toml_split_array`

```punpun
fn _toml_split_array(source: str) -> List<str>
```

### `_toml_parse_value`

```punpun
fn _toml_parse_value(source: str) -> JsonValue
```

### `toml_parse`

```punpun
fn toml_parse(source: str) -> TomlParseResult
```

### `toml_stringify`

```punpun
fn toml_stringify(values: Map<JsonValue>) -> str
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

## `stdlib/std/datetime.pp`

### `Duration`

```punpun
struct Duration
```

Date/time value types and ISO-8601 formatting/parsing in PunPun. Host primitives only provide wall/monotonic clocks and local decomposition.

### `seconds`

```punpun
public fn seconds() -> float
```

### `minutes`

```punpun
public fn minutes() -> float
```

### `hours`

```punpun
public fn hours() -> float
```

### `milliseconds`

```punpun
fn milliseconds(value:int)->Duration
```

### `seconds`

```punpun
fn seconds(value:int)->Duration
```

### `minutes`

```punpun
fn minutes(value:int)->Duration
```

### `hours`

```punpun
fn hours(value:int)->Duration
```

### `DateTime`

```punpun
struct DateTime
```

### `iso`

```punpun
public fn iso() -> str
```

### `datetime_now_local`

```punpun
fn datetime_now_local() -> DateTime
```

### `datetime_is_leap_year`

```punpun
fn datetime_is_leap_year(year:int)->bool
```

### `datetime_days_in_month`

```punpun
fn datetime_days_in_month(year:int,month:int)->int
```

### `datetime_valid`

```punpun
fn datetime_valid(value:DateTime)->bool
```

### `_days_from_civil`

```punpun
fn _days_from_civil(year:int,month:int,day:int)->int
```

### `datetime_to_epoch_ms_utc`

```punpun
fn datetime_to_epoch_ms_utc(value:DateTime)->int
```

### `_dt_two`

```punpun
fn _dt_two(v:int)->str
```

### `_dt_three`

```punpun
fn _dt_three(v:int)->str
```

### `datetime_format_iso`

```punpun
fn datetime_format_iso(value:DateTime)->str
```

### `_dt_digits`

```punpun
fn _dt_digits(source:str,start:int,count:int)->int
```

### `datetime_parse_iso`

```punpun
fn datetime_parse_iso(source:str)->DateTime
```

### `duration_between_ms`

```punpun
fn duration_between_ms(start:int,finish:int)->Duration
```

## `stdlib/std/db/kv.pp`

### `KeyValueDb`

```punpun
object KeyValueDb
```

Small durable document/key-value database implemented in PunPun. It intentionally favors correctness and inspectability over pretending to be a relational engine: one JSON object is atomically replaced on commit.

### `reload`

```punpun
public fn reload() -> bool
```

### `has`

```punpun
public fn has(key: str) -> bool
```

### `get`

```punpun
public fn get(key: str) -> JsonValue
```

### `get_string`

```punpun
public fn get_string(key: str, fallback: str) -> str
```

### `get_int`

```punpun
public fn get_int(key: str, fallback: int) -> int
```

### `get_bool`

```punpun
public fn get_bool(key: str, fallback: bool) -> bool
```

### `set`

```punpun
public fn set(key: str, value: JsonValue) -> void
```

### `set_string`

```punpun
public fn set_string(key: str, value: str) -> void
```

### `set_int`

```punpun
public fn set_int(key: str, value: int) -> void
```

### `set_bool`

```punpun
public fn set_bool(key: str, value: bool) -> void
```

### `remove`

```punpun
public fn remove(key: str) -> bool
```

### `size`

```punpun
public fn size() -> int
```

### `keys`

```punpun
public fn keys() -> List<str>
```

### `commit`

```punpun
public fn commit() -> bool
```

### `open_kv`

```punpun
fn open_kv(path: str) -> KeyValueDb
```

## `stdlib/std/filesystem.pp`

### `FileInfo`

```punpun
struct FileInfo
```

### `file_info`

```punpun
fn file_info(value:str)->FileInfo
```

### `fs_copy_file`

```punpun
fn fs_copy_file(source:str,destination:str)->void
```

### `fs_copy_tree`

```punpun
fn fs_copy_tree(source:str,destination:str,max_depth:int)->void
```

### `fs_remove_tree`

```punpun
fn fs_remove_tree(target:str,max_depth:int)->bool
```

### `fs_walk`

```punpun
fn fs_walk(root:str,max_depth:int)->List<FileInfo>
```

### `_fs_walk_into`

```punpun
fn _fs_walk_into(root:str,depth:int,out:List<FileInfo>)->void
```

### `fs_read_lines`

```punpun
fn fs_read_lines(path_value:str)->List<str>
```

### `fs_write_lines`

```punpun
fn fs_write_lines(path_value:str,lines:List<str>)->void
```

### `fs_temp_name`

```punpun
fn fs_temp_name(prefix:str,suffix:str)->str
```

### `fs_write_text_atomic`

```punpun
fn fs_write_text_atomic(path_value:str,content:str)->void
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

Retained cross-platform GUI toolkit. Native windows use Win32 on Windows and X11/XWayland on POSIX. Set PUNPUN_GUI_HEADLESS=1 for deterministic CI/model tests without a display. Every control is represented by a small runtime handle; layout and event-loop helpers live here in PunPun so all compiler backends observe identical rules.

### `gui_is_headless`

```punpun
fn gui_is_headless() -> bool
```

### `gui_alert`

```punpun
fn gui_alert(message: str) -> bool
```

### `gui_label_kind`

```punpun
fn gui_label_kind() -> int
```

Widget kinds.

### `gui_button_kind`

```punpun
fn gui_button_kind() -> int
```

### `gui_input_kind`

```punpun
fn gui_input_kind() -> int
```

### `gui_checkbox_kind`

```punpun
fn gui_checkbox_kind() -> int
```

### `gui_slider_kind`

```punpun
fn gui_slider_kind() -> int
```

### `gui_progress_kind`

```punpun
fn gui_progress_kind() -> int
```

### `gui_panel_kind`

```punpun
fn gui_panel_kind() -> int
```

### `gui_canvas_kind`

```punpun
fn gui_canvas_kind() -> int
```

### `gui_event_none`

```punpun
fn gui_event_none() -> int
```

Event kinds.

### `gui_event_close`

```punpun
fn gui_event_close() -> int
```

### `gui_event_click`

```punpun
fn gui_event_click() -> int
```

### `gui_event_change`

```punpun
fn gui_event_change() -> int
```

### `gui_event_key`

```punpun
fn gui_event_key() -> int
```

### `gui_event_text_changed`

```punpun
fn gui_event_text_changed() -> int
```

### `gui_event_mouse_move`

```punpun
fn gui_event_mouse_move() -> int
```

### `gui_event_mouse_down`

```punpun
fn gui_event_mouse_down() -> int
```

### `gui_event_mouse_up`

```punpun
fn gui_event_mouse_up() -> int
```

### `gui_event_resize`

```punpun
fn gui_event_resize() -> int
```

### `gui_event_paint`

```punpun
fn gui_event_paint() -> int
```

### `GuiEvent`

```punpun
object GuiEvent
```

### `is_close`

```punpun
public fn is_close() -> bool
```

### `is_click`

```punpun
public fn is_click() -> bool
```

### `is_change`

```punpun
public fn is_change() -> bool
```

### `is_text`

```punpun
public fn is_text() -> bool
```

### `GuiWidget`

```punpun
object GuiWidget
```

### `valid`

```punpun
public fn valid() -> bool
```

### `bounds`

```punpun
public fn bounds(x: int, y: int, width: int, height: int) -> bool
```

### `x`

```punpun
public fn x() -> int
```

### `y`

```punpun
public fn y() -> int
```

### `width`

```punpun
public fn width() -> int
```

### `height`

```punpun
public fn height() -> int
```

### `set_text`

```punpun
public fn set_text(value: str) -> bool
```

### `text`

```punpun
public fn text() -> str
```

### `set_value`

```punpun
public fn set_value(value: int) -> bool
```

### `value`

```punpun
public fn value() -> int
```

### `set_range`

```punpun
public fn set_range(minimum: int, maximum: int) -> bool
```

### `visible`

```punpun
public fn visible(value: bool) -> bool
```

### `enabled`

```punpun
public fn enabled(value: bool) -> bool
```

### `checked`

```punpun
public fn checked() -> bool
```

### `set_checked`

```punpun
public fn set_checked(value: bool) -> bool
```

### `destroy`

```punpun
public fn destroy() -> bool
```

### `clear`

```punpun
public fn clear(rgb: int) -> bool
```

Canvas drawing. These return false on non-canvas controls.

### `rect`

```punpun
public fn rect(x: int, y: int, width: int, height: int, rgb: int, filled: bool) -> bool
```

### `line`

```punpun
public fn line(x1: int, y1: int, x2: int, y2: int, rgb: int) -> bool
```

### `draw_text`

```punpun
public fn draw_text(x: int, y: int, value: str, rgb: int) -> bool
```

### `GuiWindow`

```punpun
object GuiWindow
```

### `valid`

```punpun
public fn valid() -> bool
```

### `open`

```punpun
public fn open() -> bool
```

### `width`

```punpun
public fn width() -> int
```

### `height`

```punpun
public fn height() -> int
```

### `title`

```punpun
public fn title(value: str) -> bool
```

### `show`

```punpun
public fn show() -> bool
```

### `hide`

```punpun
public fn hide() -> bool
```

### `close`

```punpun
public fn close() -> bool
```

### `redraw`

```punpun
public fn redraw() -> bool
```

### `add`

```punpun
public fn add(kind: int, text: str) -> GuiWidget
```

### `label`

```punpun
public fn label(text: str) -> GuiWidget
```

### `button`

```punpun
public fn button(text: str) -> GuiWidget
```

### `input`

```punpun
public fn input(text: str) -> GuiWidget
```

### `checkbox`

```punpun
public fn checkbox(text: str) -> GuiWidget
```

### `slider`

```punpun
public fn slider(minimum: int, maximum: int, value: int) -> GuiWidget
```

### `progress`

```punpun
public fn progress(minimum: int, maximum: int, value: int) -> GuiWidget
```

### `panel`

```punpun
public fn panel() -> GuiWidget
```

### `canvas`

```punpun
public fn canvas() -> GuiWidget
```

### `poll`

```punpun
public fn poll(timeout_ms: int) -> GuiEvent
```

### `post`

```punpun
public fn post(kind: int, widget: int, key: int, text: str, x: int, y: int) -> bool
```

### `gui_vbox`

```punpun
fn gui_vbox(widgets: List<GuiWidget>, x: int, y: int, width: int,
```

Simple backend-independent layouts. `padding` is the outer inset and `gap` separates adjacent controls. Widgets are laid out in list order.

### `gui_hbox`

```punpun
fn gui_hbox(widgets: List<GuiWidget>, x: int, y: int, width: int,
```

### `gui_grid`

```punpun
fn gui_grid(widgets: List<GuiWidget>, columns: int, x: int, y: int,
```

### `gui_run`

```punpun
fn gui_run(window: GuiWindow, handler: fn(GuiEvent) -> void) -> void
```

Run a conventional GUI event loop. The handler may be a capturing closure. Returning from the handler does not close the app; call window.close() when the application is finished.

### `gui_rgb`

```punpun
fn gui_rgb(red: int, green: int, blue: int) -> int
```

### `gui_confirm`

```punpun
fn gui_confirm(title: str, message: str) -> bool
```

## `stdlib/std/io.pp`

### `io_read_line`

```punpun
fn io_read_line() -> String
```

## `stdlib/std/logging.pp`

### `log_trace_level`

```punpun
fn log_trace_level()->int
```

### `log_level_name`

```punpun
fn log_level_name(level:int)->str
```

### `LogRecord`

```punpun
struct LogRecord
```

### `log_record_format`

```punpun
fn log_record_format(record:LogRecord,timestamps:bool)->str
```

### `Logger`

```punpun
object Logger
```

### `enabled`

```punpun
public fn enabled(level:int)->bool
```

### `log`

```punpun
public fn log(level:int,message:str)->void
```

### `trace`

```punpun
public fn trace(message:str)->void
```

### `logger`

```punpun
fn logger(name:str,threshold:int)->Logger
```

### `file_logger`

```punpun
fn file_logger(name:str,threshold:int,path_value:str)->Logger
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

## `stdlib/std/net/dns.pp`

### `dns_resolve`

```punpun
fn dns_resolve(host: str) -> List<str>
```

DNS resolution helpers. family is 0 (any), 4 (IPv4), or 6 (IPv6).

### `dns_resolve_v4`

```punpun
fn dns_resolve_v4(host: str) -> List<str>
```

### `dns_resolve_v6`

```punpun
fn dns_resolve_v6(host: str) -> List<str>
```

### `dns_resolve_async`

```punpun
async fn dns_resolve_async(host: str) -> List<str>
```

## `stdlib/std/net/http.pp`

### `HttpResponse`

```punpun
object HttpResponse
```

Structured HTTP/1.1 on top of std.net.tcp. Plain HTTP uses PunPun sockets; HTTPS uses the verified libcurl runtime but returns the same response shape. Request/response bodies are bytes so binary payloads survive unchanged.

### `ok`

```punpun
public fn ok() -> bool
```

### `text`

```punpun
public fn text() -> str
```

### `header`

```punpun
public fn header(name: str) -> str
```

### `HttpRequest`

```punpun
object HttpRequest
```

### `valid`

```punpun
public fn valid() -> bool
```

### `text`

```punpun
public fn text() -> str
```

### `header`

```punpun
public fn header(name: str) -> str
```

### `HttpUrl`

```punpun
object HttpUrl
```

### `valid`

```punpun
public fn valid() -> bool
```

### `HttpClient`

```punpun
object HttpClient
```

### `set_header`

```punpun
public fn set_header(name: str, value: str) -> void
```

### `request`

```punpun
public fn request(method: str, url: str, body: bytes) -> HttpResponse
```

### `get`

```punpun
public fn get(url: str) -> HttpResponse
```

### `delete`

```punpun
public fn delete(url: str) -> HttpResponse
```

### `post`

```punpun
public fn post(url: str, body: bytes) -> HttpResponse
```

### `put`

```punpun
public fn put(url: str, body: bytes) -> HttpResponse
```

### `patch`

```punpun
public fn patch(url: str, body: bytes) -> HttpResponse
```

### `HttpChunkHead`

```punpun
struct HttpChunkHead
```

### `http_headers`

```punpun
fn http_headers() -> Map<str>
```

### `http_header_set`

```punpun
fn http_header_set(headers: Map<str>, name: str, value: str) -> void
```

### `http_header_get`

```punpun
fn http_header_get(headers: Map<str>, name: str) -> str
```

### `_http_parse_decimal`

```punpun
fn _http_parse_decimal(value: str) -> int
```

### `_http_parse_url`

```punpun
fn _http_parse_url(url: str) -> HttpUrl
```

### `_http_header_lines`

```punpun
fn _http_header_lines(headers: Map<str>, skip_transport: bool) -> str
```

### `_https_header_lines`

```punpun
fn _https_header_lines(headers: Map<str>) -> str
```

### `_http_parse_headers_text`

```punpun
fn _http_parse_headers_text(source: str) -> Map<str>
```

### `_http_find_header_end`

```punpun
fn _http_find_header_end(data: bytes) -> int
```

### `_http_hex_digit`

```punpun
fn _http_hex_digit(value: int) -> int
```

### `_http_chunk_head`

```punpun
fn _http_chunk_head(data: bytes, start: int) -> HttpChunkHead
```

### `_http_decode_chunked`

```punpun
fn _http_decode_chunked(data: bytes) -> bytes
```

### `_http_reason`

```punpun
fn _http_reason(status: int) -> str
```

### `_http_response_from_wire`

```punpun
fn _http_response_from_wire(wire: bytes, transport_error: str) -> HttpResponse
```

### `http_request`

```punpun
fn http_request(method: str, url: str, headers: Map<str>, body: bytes,
```

### `http_get`

```punpun
fn http_get(url: str) -> HttpResponse
```

### `http_delete`

```punpun
fn http_delete(url: str) -> HttpResponse
```

### `http_post_text`

```punpun
fn http_post_text(url: str, body: str, content_type: str) -> HttpResponse
```

### `http_response_text`

```punpun
fn http_response_text(status: int, body: str, content_type: str) -> HttpResponse
```

### `http_read_request`

```punpun
fn http_read_request(stream_handle: int, max_body_bytes: int, timeout_ms: int) -> HttpRequest
```

### `http_write_response`

```punpun
fn http_write_response(stream_handle: int, response: HttpResponse, timeout_ms: int) -> bool
```

### `http_serve_once`

```punpun
fn http_serve_once(listener_handle: int, handler: fn(HttpRequest) -> HttpResponse,
```

### `http_request_async`

```punpun
async fn http_request_async(method: str, url: str, headers: Map<str>, body: bytes,
```

### `http_get_async`

```punpun
async fn http_get_async(url: str) -> HttpResponse
```

### `http_post_text_async`

```punpun
async fn http_post_text_async(url: str, body: str, content_type: str) -> HttpResponse
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

## `stdlib/std/net/tcp.pp`

### `TcpStream`

```punpun
object TcpStream
```

Portable TCP sockets. Runtime handles are nonblocking internally; every operation takes a timeout and observes task cancellation while waiting.

### `valid`

```punpun
public fn valid() -> bool
```

### `peer_host`

```punpun
public fn peer_host() -> str
```

### `peer_port`

```punpun
public fn peer_port() -> int
```

### `local_port`

```punpun
public fn local_port() -> int
```

### `set_nodelay`

```punpun
public fn set_nodelay(enabled: bool) -> bool
```

### `wait_readable`

```punpun
public fn wait_readable(timeout_ms: int) -> bool
```

### `wait_writable`

```punpun
public fn wait_writable(timeout_ms: int) -> bool
```

### `send`

```punpun
public fn send(data: bytes, timeout_ms: int) -> int
```

### `send_text`

```punpun
public fn send_text(data: str, timeout_ms: int) -> int
```

### `recv`

```punpun
public fn recv(max_bytes: int, timeout_ms: int) -> bytes
```

### `recv_text`

```punpun
public fn recv_text(max_bytes: int, timeout_ms: int) -> str
```

### `shutdown_read`

```punpun
public fn shutdown_read() -> bool
```

### `shutdown_write`

```punpun
public fn shutdown_write() -> bool
```

### `shutdown`

```punpun
public fn shutdown() -> bool
```

### `close`

```punpun
public fn close() -> bool
```

### `TcpListener`

```punpun
object TcpListener
```

### `valid`

```punpun
public fn valid() -> bool
```

### `port`

```punpun
public fn port() -> int
```

### `accept`

```punpun
public fn accept(timeout_ms: int) -> TcpStream
```

### `close`

```punpun
public fn close() -> bool
```

### `tcp_connect`

```punpun
fn tcp_connect(host: str, port: int, timeout_ms: int) -> TcpStream
```

### `tcp_listen`

```punpun
fn tcp_listen(host: str, port: int, backlog: int) -> TcpListener
```

### `tcp_connect_async`

```punpun
async fn tcp_connect_async(host: str, port: int, timeout_ms: int) -> TcpStream
```

### `tcp_accept_async`

```punpun
async fn tcp_accept_async(listener_handle: int, timeout_ms: int) -> TcpStream
```

## `stdlib/std/net/udp.pp`

### `UdpPacket`

```punpun
object UdpPacket
```

Datagram sockets. recv_from records source address and port atomically with the packet and exposes them as one UdpPacket object.

### `text`

```punpun
public fn text() -> str
```

### `UdpSocket`

```punpun
object UdpSocket
```

### `valid`

```punpun
public fn valid() -> bool
```

### `port`

```punpun
public fn port() -> int
```

### `send_to`

```punpun
public fn send_to(host: str, port: int, data: bytes, timeout_ms: int) -> int
```

### `send_text_to`

```punpun
public fn send_text_to(host: str, port: int, data: str, timeout_ms: int) -> int
```

### `recv_from`

```punpun
public fn recv_from(max_bytes: int, timeout_ms: int) -> UdpPacket
```

### `wait_readable`

```punpun
public fn wait_readable(timeout_ms: int) -> bool
```

### `close`

```punpun
public fn close() -> bool
```

### `udp_bind`

```punpun
fn udp_bind(host: str, port: int) -> UdpSocket
```

### `udp_recv_from_async`

```punpun
async fn udp_recv_from_async(socket_handle: int, max_bytes: int, timeout_ms: int) -> UdpPacket
```

## `stdlib/std/net/websocket.pp`

### `WebSocketMessage`

```punpun
object WebSocketMessage
```

RFC 6455 WebSocket support over ws://. The frame layer handles masking, fragmentation, ping/pong, close frames, binary payloads, and timeout-aware reads. wss:// deliberately waits for a reviewed raw TLS stream binding rather than implementing TLS in PunPun.

### `valid`

```punpun
public fn valid() -> bool
```

### `is_text`

```punpun
public fn is_text() -> bool
```

### `is_binary`

```punpun
public fn is_binary() -> bool
```

### `text`

```punpun
public fn text() -> str
```

### `WebSocket`

```punpun
object WebSocket
```

### `valid`

```punpun
public fn valid() -> bool
```

### `send_text`

```punpun
public fn send_text(value: str, timeout_ms: int) -> bool
```

### `send_binary`

```punpun
public fn send_binary(value: bytes, timeout_ms: int) -> bool
```

### `ping`

```punpun
public fn ping(value: bytes, timeout_ms: int) -> bool
```

### `recv`

```punpun
public fn recv(timeout_ms: int) -> WebSocketMessage
```

### `close`

```punpun
public fn close(timeout_ms: int) -> bool
```

### `WsFrame`

```punpun
object WsFrame
```

### `_ws_rotl32`

```punpun
fn _ws_rotl32(value: int, amount: int) -> int
```

### `_ws_sha1`

```punpun
fn _ws_sha1(input: bytes) -> bytes
```

### `_ws_accept_value`

```punpun
fn _ws_accept_value(key: str) -> str
```

### `_ws_recv_exact`

```punpun
fn _ws_recv_exact(handle: int, count: int, timeout_ms: int) -> bytes
```

### `_ws_send_frame`

```punpun
fn _ws_send_frame(handle: int, client_side: bool, opcode: int, payload: bytes,
```

### `_ws_read_frame`

```punpun
fn _ws_read_frame(handle: int, client_side: bool, timeout_ms: int) -> WsFrame
```

### `_ws_recv_message`

```punpun
fn _ws_recv_message(handle: int, client_side: bool, timeout_ms: int) -> WebSocketMessage
```

### `websocket_accept`

```punpun
fn websocket_accept(stream_handle: int, request: HttpRequest, timeout_ms: int) -> WebSocket
```

### `websocket_connect`

```punpun
fn websocket_connect(url: str, timeout_ms: int) -> WebSocket
```

### `websocket_connect_async`

```punpun
async fn websocket_connect_async(url: str, timeout_ms: int) -> WebSocket
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

## `stdlib/std/path.pp`

### `Path`

```punpun
struct Path
```

Platform-neutral lexical path manipulation in PunPun. No filesystem access is required for normalization/relative calculations.

### `name`

```punpun
public fn name()->str
```

### `parent`

```punpun
public fn parent()->Path
```

### `extension`

```punpun
public fn extension()->str
```

### `join`

```punpun
public fn join(child:str)->Path
```

### `normalized`

```punpun
public fn normalized()->Path
```

### `absolute`

```punpun
public fn absolute()->bool
```

### `path`

```punpun
fn path(value:str)->Path
```

### `path_is_separator`

```punpun
fn path_is_separator(c:int)->bool
```

### `path_is_absolute`

```punpun
fn path_is_absolute(value:str)->bool
```

### `path_normalize`

```punpun
fn path_normalize(value:str)->str
```

### `path_parts`

```punpun
fn path_parts(value:str)->List<str>
```

### `path_relative`

```punpun
fn path_relative(from_path:str,to_path:str)->str
```

### `path_change_extension`

```punpun
fn path_change_extension(value:str,extension:str)->str
```

## `stdlib/std/process.pp`

### `ProcessResult`

```punpun
struct ProcessResult
```

Process helpers. Runtime primitive only executes/captures a shell command; quoting, result modelling, argument assembly and async wrappers live here.

### `ok`

```punpun
public fn ok()->bool
```

### `lines`

```punpun
public fn lines()->List<str>
```

### `shell_quote_posix`

```punpun
fn shell_quote_posix(value:str)->str
```

### `shell_quote_windows`

```punpun
fn shell_quote_windows(value:str)->str
```

### `shell_quote`

```punpun
fn shell_quote(value:str)->str
```

### `process_command`

```punpun
fn process_command(program:str,args:List<str>)->str
```

### `process_run`

```punpun
fn process_run(command:str)->ProcessResult
```

### `process_run_args`

```punpun
fn process_run_args(program:str,args:List<str>)->ProcessResult
```

### `process_capture_async`

```punpun
async fn process_capture_async(command:str)->str
```

### `process_status_async`

```punpun
async fn process_status_async(command:str)->int
```

## `stdlib/std/random.pp`

### `Random`

```punpun
object Random
```

Deterministic Park-Miller RNG written in PunPun. The Schrage step avoids overflow under PunPun's checked integer arithmetic.

### `next_raw`

```punpun
public fn next_raw() -> int
```

### `int_between`

```punpun
public fn int_between(low: int, high: int) -> int
```

### `unit`

```punpun
public fn unit() -> float
```

### `chance`

```punpun
public fn chance(probability: float) -> bool
```

### `random_generator`

```punpun
fn random_generator(seed: int) -> Random
```

### `secure_token_hex`

```punpun
fn secure_token_hex(byte_count: int) -> str
```

### `secure_token_urlsafe`

```punpun
fn secure_token_urlsafe(byte_count: int) -> str
```

## `stdlib/std/regex.pp`

### `RegexMatch`

```punpun
struct RegexMatch
```

Small backtracking regular-expression engine implemented in PunPun. Supported: literals, ., ^, $, escapes (\d \w \s), character classes/ranges, negated classes, and greedy *, +, ?. Search/replace/split are built on it.

### `regex_no_match`

```punpun
fn regex_no_match() -> RegexMatch
```

### `_regex_class_end`

```punpun
fn _regex_class_end(pattern: str, start: int) -> int
```

### `_regex_named_class`

```punpun
fn _regex_named_class(kind: int, c: int) -> bool
```

### `_regex_class_matches`

```punpun
fn _regex_class_matches(pattern: str, start: int, finish: int, c: int) -> bool
```

### `_regex_atom_end`

```punpun
fn _regex_atom_end(pattern: str, at: int) -> int
```

### `_regex_atom_matches`

```punpun
fn _regex_atom_matches(pattern: str, at: int, atom_end: int, text_value: str, pos: int) -> bool
```

### `_regex_match_from`

```punpun
fn _regex_match_from(pattern: str, p: int, text_value: str, t: int, depth: int) -> int
```

### `regex_search_from`

```punpun
fn regex_search_from(pattern: str, text_value: str, offset: int) -> RegexMatch
```

### `regex_search`

```punpun
fn regex_search(pattern: str, text_value: str) -> RegexMatch
```

### `regex_is_match`

```punpun
fn regex_is_match(pattern: str, text_value: str) -> bool
```

### `regex_full_match`

```punpun
fn regex_full_match(pattern: str, text_value: str) -> bool
```

### `regex_find_all`

```punpun
fn regex_find_all(pattern: str, text_value: str) -> List<str>
```

### `regex_replace`

```punpun
fn regex_replace(pattern: str, text_value: str, replacement: str) -> str
```

### `regex_split`

```punpun
fn regex_split(pattern: str, text_value: str) -> List<str>
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

## `stdlib/std/system_info.pp`

### `SystemInfo`

```punpun
struct SystemInfo
```

Small host-information layer over unavoidable OS queries.

### `system_info`

```punpun
fn system_info()->SystemInfo
```

### `environment`

```punpun
fn environment(name:str,fallback:str)->str
```

### `environment_set`

```punpun
fn environment_set(name:str,value:str)->bool
```

### `arguments`

```punpun
fn arguments()->List<str>
```

## `stdlib/std/testing.pp`

### `expect_int`

```punpun
fn expect_int(actual: i64, expected: i64) -> void
```

Assertion, aggregation and benchmark helpers written in PunPun.

### `expect_bool`

```punpun
fn expect_bool(actual: bool, expected: bool) -> void
```

### `expect_str`

```punpun
fn expect_str(actual: String, expected: String) -> void
```

### `expect_close`

```punpun
fn expect_close(actual:float,expected:float,tolerance:float)->void
```

### `TestSuite`

```punpun
object TestSuite
```

### `check`

```punpun
public fn check(condition:bool,message:str)->void
```

### `equal_int`

```punpun
public fn equal_int(actual:int,expected:int,message:str)->void
```

### `equal_str`

```punpun
public fn equal_str(actual:str,expected:str,message:str)->void
```

### `equal_bool`

```punpun
public fn equal_bool(actual:bool,expected:bool,message:str)->void
```

### `ok`

```punpun
public fn ok()->bool
```

### `summary`

```punpun
public fn summary()->str
```

### `assert_ok`

```punpun
public fn assert_ok()->void
```

### `Benchmark`

```punpun
struct Benchmark
```

### `benchmark`

```punpun
fn benchmark(rounds:int,work:fn()->void)->Benchmark
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

## `stdlib/std/text/utf8.pp`

### `utf8_encode_codepoint`

```punpun
fn utf8_encode_codepoint(codepoint:int)->str
```

UTF-8 encode/decode utilities implemented in PunPun over byte strings.

### `_utf8_cont`

```punpun
fn _utf8_cont(c:int)->bool
```

### `utf8_decode`

```punpun
fn utf8_decode(value:str)->List<int>
```

### `utf8_encode`

```punpun
fn utf8_encode(codepoints:List<int>)->str
```

### `utf8_reverse`

```punpun
fn utf8_reverse(value:str)->str
```

### `utf8_at`

```punpun
fn utf8_at(value:str,index:int)->int
```

### `utf8_slice`

```punpun
fn utf8_slice(value:str,start:int,end:int)->str
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
