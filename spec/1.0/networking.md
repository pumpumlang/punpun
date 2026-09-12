# Networking contract

This document defines the portable networking behavior shared by the C,
direct-native and bytecode backends. It specifies observable semantics; native
socket descriptor values and operating-system implementation details are not
part of the language ABI.

## Socket handles

DNS, TCP and UDP operations are runtime services. A PunPun socket is a positive
runtime-owned integer handle, not a POSIX file descriptor or Winsock `SOCKET`.
The runtime maps that handle to the host resource and validates it before every
operation. Closing a handle invalidates it. Programs must not close a handle
concurrently with another operation using the same handle.

All runtime sockets are placed in nonblocking mode. Operations that need to wait
use readiness polling in bounded slices so task cancellation can be observed.
A timeout is reported through the thread-local networking status rather than by
blocking the host thread forever.

The last-operation values `net_error()`, `net_timed_out()`, `net_eof()`,
`net_last_host()` and `net_last_port()` are thread-local. A networking operation
resets the status before doing work; callers that need an error after cleanup
must copy it before issuing another network operation.

## DNS

Resolution accepts IPv4, IPv6 or either family and returns numeric address
strings. Resolution order is host-platform defined. TCP connect tries viable
resolved addresses. UDP send selects a resolved destination matching the bound
socket's actual address family instead of assuming the first DNS result is
compatible.

## TCP

TCP supports connect, listen, accept, readiness waits, binary send/receive,
local and peer addressing, `TCP_NODELAY`, half shutdown and full shutdown.
`send` attempts to transmit the entire supplied byte buffer before its timeout.
A receive returning no bytes is distinguished as timeout, EOF or error through
the last-operation status.

The runtime caps a single TCP receive request at 64 MiB. Higher-level protocols
may impose smaller limits.

## UDP

UDP supports bind, binary datagram send and receive, local-port discovery and
source host/port reporting. One send corresponds to one datagram. Payloads are
limited to 65,507 bytes. Receiving preserves the sender's numeric address and
port in the thread-local last-source fields used by `std.net.udp`.

## HTTP/1.1

`std.net.http` uses PunPun TCP sockets for `http://` and the reviewed libcurl
runtime for `https://`. Both paths expose the same structured response:
status, reason, case-normalized header map, binary body and error string.

Plain HTTP client requests currently use `Connection: close`; there is no
connection pool. Response parsing supports content-length, chunked transfer
encoding and connection-close framing. Response bodies are capped at 64 MiB.
Server request headers are capped at 1 MiB and callers select a body limit up to
64 MiB. Header names/values that could inject CR/LF are rejected.

HTTPS MUST retain peer-certificate verification and hostname verification.
Redirects issued by the HTTPS runtime are restricted to HTTPS. Async HTTPS
helpers may execute blocking libcurl work inside runtime worker tasks; this is
not equivalent to the nonblocking raw socket implementation and must not be
described as such.

## WebSocket

`std.net.websocket` implements RFC 6455 framing over `ws://`. Client frames are
masked; server frames are not. Text, binary, continuation, ping, pong and close
opcodes are supported. Fragmented data messages are reassembled while control
frames may appear between fragments. Ping is answered with pong before message
receive continues.

Individual frames and assembled messages are capped at 16 MiB. RSV bits are
rejected unless a future extension explicitly negotiates their meaning.
Control frames must be final and at most 125 bytes.

`wss://` is intentionally unsupported until PunPun exposes a reviewed raw TLS
stream binding. Implementing TLS or certificate validation in ordinary PunPun
code is not an acceptable substitute.

## Backend equivalence

The compiler exposes networking as runtime builtins, so generated C, direct
x86-64 and bytecode invoke the same runtime contract. Tests for TCP/UDP/DNS,
structured HTTP, chunked decoding, closure-based HTTP handlers and WebSocket
handshake/framing run across all three backends. A backend must reject a
networking feature explicitly if its host cannot provide the runtime service;
it may not silently change protocol semantics.
