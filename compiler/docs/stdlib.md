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

### HTTP and networking

Blocked on socket primitives, which do not exist in the runtime yet. The
ordering is sockets, then HTTP/1.1, then TLS — and TLS has the same
"do not write it yourself" constraint as encryption, so HTTPS means binding a
reviewed library.

Writing an HTTP module today would produce something that cannot connect.

### GUI

Needs a windowing binding per platform, which is a large amount of
platform-specific code that cannot be tested in a headless environment. Nothing
here would be verifiable, so nothing here was written.

## Next, in order

1. **Sockets** in the runtime, then HTTP/1.1 on top.
2. **SHA-256 and HMAC** as native builtins.
3. **JSON**, which needs recursive types — currently rejected with E0901.
4. **Recursive types via boxing**, which unblocks JSON, trees, and self-hosting.
5. Bindings to libsodium and a TLS library for real cryptography and HTTPS.
