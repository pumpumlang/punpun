# Standard library

## Status, stated plainly

**47 modules and 143 builtins**, all tested across the C, native, and bytecode
backends. That is not the "hundreds of libraries" that was asked for, and this
document does not pretend otherwise.

What was built instead is the layer everything else has to stand on. Before this
work `nums` (a list of int) was the only collection in the language; a program
could not hold a list of strings, a map, or a byte buffer. No amount of library
code fixes that from above.

## What exists now

### Primitives (runtime + compiler)

| type | purpose |
|---|---|
| `List<T>` | growable sequence of any element type, including structs and enums |
| `Map<V>` | string-keyed hash map, open addressing with tombstones |
| `bytes` | mutable binary buffer, distinct from `str` because `str` is UTF-8 text |
| `nums` | unchanged, the int-specific list, kept for source compatibility |

### Builtins, by area

- **Text** (17): `char_at`, `char_str`, `index_of`, `last_index_of`,
  `starts_with`, `ends_with`, `to_upper`, `to_lower`, `trim`, `replace`,
  `repeat`, `split`, `join`, `text_float`, `parse_float`, `pad_left`,
  `pad_right`
- **Math** (21): `sqrt`, `pow`, `exp`, `log`, `log2`, `log10`, `sin`, `cos`,
  `tan`, `asin`, `acos`, `atan`, `atan2`, `floor`, `ceil`, `round`, `fabs`,
  `fmod`, `hypot`, `is_nan`, `is_infinite`
- **Collections** (16): the `List` and `Map` surfaces
- **bytes** (9)
- **Filesystem** (11): directory listing, binary read/write, path decomposition,
  recursive directory creation
- **Time** (8): wall clock plus local-time breakdown
- **Random** (4): a seeded xorshift generator, and `random_bytes` from the OS
- **Process** (2): `exit`, `run_command`

### Modules

```
std/collections/  stack  queue  set  counter  sorting  search  list_ops
                  heap  grid
std/text/         strings  format  casing  distance  wrap
std/data/         hex  base64  checksum  csv  query  ini  binary  uuid
std/math_ext/     integers  bits  vector  constants  floats  statistics
                  shuffling
std/system_ext/   cli  console  log  timer  paths  files
std/              async fs io math nums option result stats system testing
                  text time
```

Highlights:

- **collections** — stack, queue, set, counter, binary min-heap, 2D grid, and
  list operations. Sorting is median-of-three quicksort with an insertion-sort
  cutoff; a first-element pivot degrades to O(n^2) on sorted input, the most
  common real shape.
- **data** — CRC-32, Adler-32, FNV-1a, CSV (RFC 4180, including quoted fields
  with embedded commas and doubled quotes), URL and query encoding, INI,
  fixed-width and varint binary encoding, UUID v4.
- **math_ext** — integer theory (gcd, primes by sieve, modular exponentiation,
  integer sqrt by Newton), bit manipulation, 2D/3D vectors, statistics with both
  population and sample variance, Fisher-Yates shuffling and Box-Muller
  gaussians.
- **system_ext** — argument parsing, ANSI console styling, levelled logging,
  stopwatch, path manipulation, recursive directory walking, atomic file
  writes.

## A note on randomness

There are deliberately two generators, and mixing them up would be a real
security bug:

- `random_int` / `random_float` — seeded xorshift. Fast, reproducible for a given
  seed, and **predictable from its output**. For simulation, shuffling, and
  tests.
- `random_bytes` — reads the operating system's entropy pool. For keys, nonces,
  tokens, and salts.

## Networking

Networking is split by layer instead of hiding every operation behind the HTTP
client:

- `std.net.dns` resolves hosts to IPv4/IPv6 address lists.
- `std.net.tcp` provides connect/listen/accept, binary send/receive, half/full
  shutdown, peer/local addressing, `TCP_NODELAY`, readiness waits, timeouts and
  async wrappers.
- `std.net.udp` provides bind, binary/text datagrams, source addresses and
  timeout-aware receive.
- `std.net.http` provides structured HTTP/1.1 client and server values, header
  maps, binary request/response bodies, chunked decoding and closure handlers.
  Plain HTTP uses PunPun sockets; HTTPS uses the verified libcurl runtime and
  returns the same `HttpResponse` shape.
- `std.net.websocket` provides RFC 6455 `ws://` client/server framing, masking,
  fragmentation, ping/pong, close frames and text/binary messages.

Runtime socket handles are PunPun-owned integers, not exposed OS descriptors.
The underlying sockets are nonblocking and wait in cancellation-aware slices,
so a cancelled task is not trapped indefinitely in `accept`, `connect`, `send`
or `recv`. The high-level HTTP client currently closes after each request; a
keep-alive connection pool is intentionally future work.

## What is deliberately absent

### Encryption

There is no AES, RSA, or ChaCha implementation here, and adding one written from
scratch would be irresponsible. Cipher implementations fail through timing
side channels, padding oracles, and nonce reuse — failure modes that unit tests
pass straight through. A hand-rolled cipher that produces the right bytes can
still leak the key.

The intended path is hashing and encoding here, plus a binding to a reviewed
library (libsodium or OpenSSL) through `extern native fn` for anything
requiring actual secrecy. Hashing (SHA-256, HMAC) is safe to implement natively
and is planned; encryption is not.

### Raw TLS streams

HTTPS is available through the reviewed libcurl binding, but PunPun does not yet
expose a raw TLS stream abstraction. `wss://` therefore remains unavailable even
though `ws://` WebSockets and verified `https://` requests work. The eventual
implementation must bind a reviewed TLS library rather than implement TLS in the
language.

### GUI

Needs a windowing binding per platform, which is a large amount of
platform-specific code that cannot be tested in a headless environment. Nothing
here would be verifiable, so nothing here was written.

## Next, in order

1. **Raw TLS streams** backed by a reviewed TLS library, which unlock `wss://`
   without duplicating cryptography in PunPun.
2. **HTTP keep-alive pooling** and streaming request/response bodies for clients
   that need long-lived high-throughput connections.
3. **SHA-256 and HMAC** as native builtins.
4. **JSON** and richer serialization APIs.
5. Bindings to libsodium for application cryptography.
