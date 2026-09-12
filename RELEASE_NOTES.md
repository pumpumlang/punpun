# PunPun 1.5.0 release notes

PunPun 1.5.0 is the halfway milestone in the 1.5 series. It takes the six
completed upgrade tracks that were staged after 1.4.5 and publishes them as one
coherent release: capturing closures, general iteration, a stronger native
backend, a real networking stack, a retained GUI toolkit, and a substantially
broader standard library written primarily in PunPun itself.

The stable 1.x language/runtime ABI remains epoch 1. Existing 1.x source stays
within the compatibility contract.

## Capturing closures

Function literals now capture free locals by value into owned environments.
Mutable captures persist across calls, copied closure handles share their
captured environment, closures may escape their creating function, nested
closures propagate grandparent captures, and move-only values transfer ownership
into the closure. Borrowed captures (`&T`, `&mut T`, `Slice<T>`) remain rejected
until lifetime-aware closure escape analysis can prove them safe.

Zero-capture function values remain allocation-free. C, bytecode, and direct
x86-64 use the same one-word closure-handle model.

## General iterator protocol

`for value in source` is no longer limited to built-in sequences. User-defined
iterables expose `iter()`, whose result exposes `advance() -> Option<T>`;
iterator objects can also be looped directly. This supports custom, lazy, and
unbounded producers without materializing a list. Existing `nums`, `List<T>`
and `Slice<T>` keep their indexed fast path.

## Native x86-64 backend

The direct backend now has CFG-aware linear-scan allocation across callee-saved
registers, System V stack argument passing beyond the GP/SSE register limits,
native async task spawning/await/cancellation/task-group support, and
conservative interprocedural copy elimination for read-only value-struct
parameters. Several ABI and escape-analysis bugs exposed by the new libraries
were fixed as part of this work.

## Networking

The runtime and `std.net` now provide backend-equivalent DNS, TCP and UDP with
portable socket handles, nonblocking operation, readiness/timeouts and task
cancellation awareness. `std.net.http` adds structured HTTP/1.1 client/server
support with headers, binary bodies, chunked decoding and closure handlers.
HTTPS keeps verified libcurl transport while sharing the structured response
shape. RFC 6455 `ws://` WebSockets include masking, fragmentation, ping/pong,
close frames and bounded messages.

Raw TLS streams/`wss://` and HTTP connection pooling remain future work rather
than being approximated with unsafe home-grown crypto.

## GUI toolkit

`std.gui` has grown from the original native-window foundation into a retained
application toolkit: windows, labels, buttons, text inputs, checkboxes, sliders,
progress bars, panels, canvases, widget state, vertical/horizontal/grid layout,
mouse/keyboard/text/change/resize/paint/close events, closure-driven event loops,
canvas drawing, alerts and confirmation dialogs. A headless retained backend
makes GUI logic testable in CI. X11 is exercised with real virtual-display input;
Win32/GDI support is implemented for the same public model.

## Standard library expansion

The bulk of the new library logic is PunPun code, with native shims kept to host
operations that genuinely require the OS/runtime.

Highlights include:

- recursive `std.data.json` parsing/serialization/pretty-printing with Unicode
  escape handling and typed accessors;
- TOML/config parsing;
- a PunPun regex engine with classes, anchors, quantifiers, search, replace and
  split;
- pure-PunPun SHA-256 and HMAC-SHA256 verified against published vectors;
- deterministic RNG helpers plus separately sourced secure OS randomness;
- RLE and LZSS compression;
- interoperable method-0 ZIP archives with CRC-32 validation;
- an atomic JSON-backed durable key/value database;
- richer paths, filesystem, process, date/time, logging, testing and system APIs;
- UTF-8 and MIME helpers;
- generic deque and higher-order `map`, `filter`, `fold`, predicates, `zip` and
  `enumerate` utilities that work with generic function types and capturing
  closures.

The JSON, logging, filesystem and testing first-party packages now reuse the
full standard modules instead of maintaining toy parallel implementations.

## Validation

The staged 1.5.0 source passed 154 compiler regression cases across the
applicable C, bytecode and native backends; O0/O1/O2 standard-library matrices;
first-party HTTPS, GUI, requests, JSON, logging, filesystem and testing package
suites; compiler-native LSP tests; native structural codegen checks; self-host
bootstrap fixed point; ABI epoch-1 validation; the backend compatibility matrix;
stress testing; deterministic frontend fuzzing; documentation link checks;
privacy audit; version/stable-surface/platform-policy checks; PPX integration;
and release-hygiene checks.

The Windows implementation has been source-integrated throughout, but changes
added after 1.4.5 still rely on the platform release workflow for final Windows
runtime qualification.
