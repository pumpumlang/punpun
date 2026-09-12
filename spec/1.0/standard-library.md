# Standard library implementation contract

The stable 1.x standard library is part of the language distribution, but it is
not an excuse to move normal application logic into hidden runtime code.

## Boundary

A standard module SHOULD be implemented in PunPun whenever the operation can be
expressed safely and reasonably with existing language/runtime primitives.
Runtime C/C++ is reserved for host capabilities that PunPun cannot directly
perform, such as opening an OS process, querying the hostname, obtaining secure
random bytes, operating sockets, or presenting a native window.

Parsing, serialization, formatting, path policy, collection algorithms,
cryptographic transforms, compression algorithms and high-level error/result
models belong in `.pp` code. This keeps behavior backend-equivalent and makes
the standard library continuously exercise the public language.

## Current batteries

The current development surface includes JSON and TOML, layered configuration,
regex search/replace/split, SHA-256/HMAC-SHA256, deterministic and secure-random
helpers, LZSS/RLE compression, stored-entry ZIP archives, higher-order generic
collections and deque, UTF-8 and MIME utilities, date/time, richer filesystem
and path operations, logging/testing helpers, process capture/system metadata,
and a small atomic JSON-backed key/value database.

## Portability

Public PunPun modules must type-check on the shared frontend and their supported
behavior is tested through C, bytecode and direct native backends. Platform
primitives may differ internally, but their PunPun-visible result contract must
not. A platform is not promoted merely because another platform builds.

## Explicit limits

`std.archive.zip` currently writes and reads standard ZIP method-0 entries and
does not claim DEFLATE or ZIP64. `std.db.kv` is an atomic document/key-value
store and does not claim SQL, transactions across processes, or relational
semantics. `std.regex` intentionally documents its supported expression subset
rather than silently treating unsupported PCRE syntax as equivalent.
