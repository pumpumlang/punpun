#define _POSIX_C_SOURCE 200809L
/* Portable socket networking for PunPun.
 *
 * Sockets are always placed in nonblocking mode. The public operations provide
 * timeout-based waits implemented with select(), sliced into short intervals so
 * a PunPun task cancellation request is observed promptly. Small runtime handle
 * ids are exposed to generated code instead of native descriptors: Windows
 * SOCKET values are pointer-sized and are not POSIX file descriptors.
 */
#include "ppcrt.h"
#include "ppc_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PPC_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET pp_native_socket;
typedef int pp_socklen_t;
#  define PP_BAD_SOCKET INVALID_SOCKET
#  define pp_close_socket closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int pp_native_socket;
typedef socklen_t pp_socklen_t;
#  define PP_BAD_SOCKET (-1)
#  define pp_close_socket close
#endif

#if defined(_MSC_VER)
#  define PP_THREAD_LOCAL __declspec(thread)
#else
#  define PP_THREAD_LOCAL _Thread_local
#endif

#define PP_NET_MAX_SOCKETS 4096
#define PP_NET_WAIT_SLICE_MS 50
#define PP_NET_MAX_IO (64 * 1024 * 1024)

typedef struct pp_net_entry {
    pp_native_socket socket;
    int used;
    int datagram;
} pp_net_entry;

static pp_net_entry g_sockets[PP_NET_MAX_SOCKETS];
static ppc_plat_mutex *g_net_lock;
static int g_net_initialized;
static int g_net_available;
static char g_net_init_error[256];

static PP_THREAD_LOCAL char g_net_error[512];
static PP_THREAD_LOCAL int g_net_timed_out;
static PP_THREAD_LOCAL int g_net_eof;
static PP_THREAD_LOCAL char g_net_last_host[128];
static PP_THREAD_LOCAL int64_t g_net_last_port;

static void pp_net_reset_status(void) {
    g_net_error[0] = '\0';
    g_net_timed_out = 0;
    g_net_eof = 0;
    g_net_last_host[0] = '\0';
    g_net_last_port = 0;
}

#if PPC_WINDOWS
static int pp_net_native_error(void) { return WSAGetLastError(); }
static int pp_net_would_block(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
}
static const char *pp_net_error_text(int error) {
    static PP_THREAD_LOCAL char text[64];
    snprintf(text, sizeof(text), "Winsock error %d", error);
    return text;
}
#else
static int pp_net_native_error(void) { return errno; }
static int pp_net_would_block(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS || error == EALREADY;
}
static const char *pp_net_error_text(int error) { return strerror(error); }
#endif

static void pp_net_set_error(const char *operation, int error) {
    snprintf(g_net_error, sizeof(g_net_error), "%s: %s", operation,
             pp_net_error_text(error));
}

static void pp_net_set_message(const char *message) {
    snprintf(g_net_error, sizeof(g_net_error), "%s", message ? message : "network error");
}

static int pp_net_set_nonblocking(pp_native_socket socket) {
#if PPC_WINDOWS
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int64_t pp_net_register(pp_native_socket socket, int datagram) {
    if (socket == PP_BAD_SOCKET) return -1;
    ppc_plat_mutex_lock(g_net_lock);
    for (int i = 0; i < PP_NET_MAX_SOCKETS; ++i) {
        if (g_sockets[i].used) continue;
        g_sockets[i].socket = socket;
        g_sockets[i].used = 1;
        g_sockets[i].datagram = datagram;
        ppc_plat_mutex_unlock(g_net_lock);
        return (int64_t)i + 1;
    }
    ppc_plat_mutex_unlock(g_net_lock);
    pp_close_socket(socket);
    pp_net_set_message("network socket table is full");
    return -1;
}

static int pp_net_lookup(int64_t handle, pp_native_socket *socket, int *datagram) {
    if (handle <= 0 || handle > PP_NET_MAX_SOCKETS) {
        pp_net_set_message("invalid socket handle");
        return 0;
    }
    const int index = (int)handle - 1;
    ppc_plat_mutex_lock(g_net_lock);
    if (!g_sockets[index].used) {
        ppc_plat_mutex_unlock(g_net_lock);
        pp_net_set_message("socket handle is closed");
        return 0;
    }
    if (socket) *socket = g_sockets[index].socket;
    if (datagram) *datagram = g_sockets[index].datagram;
    ppc_plat_mutex_unlock(g_net_lock);
    return 1;
}

static int pp_net_wait_native(pp_native_socket socket, int write_ready, int64_t timeout_ms) {
    const int64_t started = ppc_plat_monotonic_ms();
    for (;;) {
        if (pp_task_cancelled()) {
            pp_net_set_message("network operation cancelled");
            return 0;
        }

        int64_t remaining = timeout_ms;
        if (timeout_ms >= 0) {
            const int64_t elapsed = ppc_plat_monotonic_ms() - started;
            remaining = timeout_ms - elapsed;
            if (remaining < 0) {
                g_net_timed_out = 1;
                return 0;
            }
        }
        int64_t slice = remaining;
        if (timeout_ms < 0 || slice > PP_NET_WAIT_SLICE_MS) slice = PP_NET_WAIT_SLICE_MS;
        if (slice < 0) slice = PP_NET_WAIT_SLICE_MS;

        fd_set read_set, write_set, error_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        if (write_ready) FD_SET(socket, &write_set);
        else FD_SET(socket, &read_set);
        FD_SET(socket, &error_set);
        struct timeval timeout;
        timeout.tv_sec = (long)(slice / 1000);
        timeout.tv_usec = (long)((slice % 1000) * 1000);
#if PPC_WINDOWS
        const int selected = select(0, write_ready ? NULL : &read_set,
                                    write_ready ? &write_set : NULL, &error_set, &timeout);
#else
        const int selected = select(socket + 1, write_ready ? NULL : &read_set,
                                    write_ready ? &write_set : NULL, &error_set, &timeout);
#endif
        if (selected > 0) {
            if (FD_ISSET(socket, &error_set)) {
                int error = 0;
                pp_socklen_t size = (pp_socklen_t)sizeof(error);
                if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size) == 0 && error) {
                    pp_net_set_error("socket", error);
                    return 0;
                }
            }
            return 1;
        }
        if (selected == 0) {
            if (timeout_ms >= 0 && ppc_plat_monotonic_ms() - started >= timeout_ms) {
                g_net_timed_out = 1;
                return 0;
            }
            continue;
        }
        const int error = pp_net_native_error();
#if !PPC_WINDOWS
        if (error == EINTR) continue;
#endif
        pp_net_set_error("select", error);
        return 0;
    }
}

static int pp_net_family(int64_t family) {
    if (family == 4) return AF_INET;
    if (family == 6) return AF_INET6;
    return AF_UNSPEC;
}

static int64_t pp_net_remaining(int64_t started, int64_t timeout_ms) {
    if (timeout_ms < 0) return -1;
    const int64_t elapsed = ppc_plat_monotonic_ms() - started;
    const int64_t remaining = timeout_ms - elapsed;
    return remaining > 0 ? remaining : 0;
}

static void pp_net_store_address(const struct sockaddr *address, pp_socklen_t length) {
    char host[128] = {0};
    char service[32] = {0};
    const int result = getnameinfo(address, length, host, (pp_socklen_t)sizeof(host),
                                   service, (pp_socklen_t)sizeof(service),
                                   NI_NUMERICHOST | NI_NUMERICSERV);
    if (result != 0) return;
    snprintf(g_net_last_host, sizeof(g_net_last_host), "%s", host);
    g_net_last_port = (int64_t)strtoll(service, NULL, 10);
}

void pp_net_runtime_init(void) {
    if (g_net_initialized) return;
    g_net_initialized = 1;
    g_net_lock = ppc_plat_mutex_new();
    if (!g_net_lock) {
        snprintf(g_net_init_error, sizeof(g_net_init_error), "cannot create network runtime lock");
        return;
    }
#if PPC_WINDOWS
    WSADATA data;
    const int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0) {
        snprintf(g_net_init_error, sizeof(g_net_init_error), "WSAStartup failed: %d", error);
        return;
    }
#endif
    memset(g_sockets, 0, sizeof(g_sockets));
    g_net_available = 1;
}

void pp_net_runtime_cleanup(void) {
    if (!g_net_initialized) return;
    if (g_net_lock) ppc_plat_mutex_lock(g_net_lock);
    for (int i = 0; i < PP_NET_MAX_SOCKETS; ++i) {
        if (!g_sockets[i].used) continue;
        pp_close_socket(g_sockets[i].socket);
        g_sockets[i].used = 0;
    }
    if (g_net_lock) ppc_plat_mutex_unlock(g_net_lock);
#if PPC_WINDOWS
    if (g_net_available) WSACleanup();
#endif
    g_net_available = 0;
}

bool pp_net_available(void) {
    if (!g_net_initialized) pp_net_runtime_init();
    return g_net_available != 0;
}

const char *pp_net_error(void) {
    if (g_net_error[0]) return g_net_error;
    return g_net_available ? "" : g_net_init_error;
}

bool pp_net_timed_out(void) { return g_net_timed_out != 0; }
bool pp_net_eof(void) { return g_net_eof != 0; }
const char *pp_net_last_host(void) { return g_net_last_host; }
int64_t pp_net_last_port(void) { return g_net_last_port; }

pp_list *pp_net_resolve(const char *host, int64_t family) {
    pp_net_reset_status();
    pp_list *out = pp_list_new();
    if (!pp_net_available()) {
        pp_net_set_message(g_net_init_error);
        return out;
    }
    if (!host || !*host) {
        pp_net_set_message("DNS host cannot be empty");
        return out;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = pp_net_family(family);
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    const int error = getaddrinfo(host, NULL, &hints, &addresses);
    if (error != 0) {
        snprintf(g_net_error, sizeof(g_net_error), "DNS lookup failed (%d)", error);
        return out;
    }

    char numeric[128];
    char previous[128] = {0};
    for (struct addrinfo *it = addresses; it; it = it->ai_next) {
        if (getnameinfo(it->ai_addr, (pp_socklen_t)it->ai_addrlen,
                        numeric, (pp_socklen_t)sizeof(numeric), NULL, 0,
                        NI_NUMERICHOST) != 0) continue;
        if (previous[0] && strcmp(previous, numeric) == 0) continue;
        const size_t length = strlen(numeric);
        char *copy = (char *)malloc(length + 1);
        if (!copy) continue;
        memcpy(copy, numeric, length + 1);
        const char *owned = pp_adopt_text(copy);
        pp_list_push(out, (int64_t)(intptr_t)owned);
        snprintf(previous, sizeof(previous), "%s", numeric);
    }
    freeaddrinfo(addresses);
    return out;
}

int64_t pp_net_tcp_connect(const char *host, int64_t port, int64_t timeout_ms) {
    pp_net_reset_status();
    if (!pp_net_available()) { pp_net_set_message(g_net_init_error); return -1; }
    if (!host || !*host || port < 1 || port > 65535) {
        pp_net_set_message("TCP connect needs a host and port 1..65535");
        return -1;
    }
    char service[16];
    snprintf(service, sizeof(service), "%lld", (long long)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *addresses = NULL;
    const int gai = getaddrinfo(host, service, &hints, &addresses);
    if (gai != 0) {
        snprintf(g_net_error, sizeof(g_net_error), "TCP DNS lookup failed (%d)", gai);
        return -1;
    }

    const int64_t started = ppc_plat_monotonic_ms();
    int last_error = 0;
    for (struct addrinfo *it = addresses; it; it = it->ai_next) {
        pp_native_socket socket_value = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_value == PP_BAD_SOCKET) { last_error = pp_net_native_error(); continue; }
        if (!pp_net_set_nonblocking(socket_value)) {
            last_error = pp_net_native_error();
            pp_close_socket(socket_value);
            continue;
        }
        int connected = connect(socket_value, it->ai_addr, (int)it->ai_addrlen) == 0;
        if (!connected) {
            const int error = pp_net_native_error();
            if (!pp_net_would_block(error)) {
                last_error = error;
                pp_close_socket(socket_value);
                continue;
            }
            const int64_t remaining = pp_net_remaining(started, timeout_ms);
            if (!pp_net_wait_native(socket_value, 1, remaining)) {
                last_error = pp_net_native_error();
                pp_close_socket(socket_value);
                if (g_net_timed_out || g_net_error[0]) break;
                continue;
            }
            int socket_error = 0;
            pp_socklen_t size = (pp_socklen_t)sizeof(socket_error);
            if (getsockopt(socket_value, SOL_SOCKET, SO_ERROR,
                           (char *)&socket_error, &size) != 0 || socket_error != 0) {
                last_error = socket_error ? socket_error : pp_net_native_error();
                pp_close_socket(socket_value);
                continue;
            }
        }
        int enabled = 1;
        (void)setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY,
                         (const char *)&enabled, (pp_socklen_t)sizeof(enabled));
        pp_net_store_address(it->ai_addr, (pp_socklen_t)it->ai_addrlen);
        freeaddrinfo(addresses);
        return pp_net_register(socket_value, 0);
    }
    freeaddrinfo(addresses);
    if (!g_net_timed_out && !g_net_error[0]) {
        pp_net_set_error("TCP connect", last_error ? last_error : pp_net_native_error());
    }
    return -1;
}

static int64_t pp_net_bind_socket(const char *host, int64_t port, int socktype,
                                  int protocol, int backlog, int datagram) {
    if (port < 0 || port > 65535) {
        pp_net_set_message("socket bind port must be 0..65535");
        return -1;
    }
    char service[16];
    snprintf(service, sizeof(service), "%lld", (long long)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *addresses = NULL;
    const char *node = (host && *host) ? host : NULL;
    const int gai = getaddrinfo(node, service, &hints, &addresses);
    if (gai != 0) {
        snprintf(g_net_error, sizeof(g_net_error), "socket bind lookup failed (%d)", gai);
        return -1;
    }

    int last_error = 0;
    for (struct addrinfo *it = addresses; it; it = it->ai_next) {
        pp_native_socket socket_value = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_value == PP_BAD_SOCKET) { last_error = pp_net_native_error(); continue; }
        int enabled = 1;
        (void)setsockopt(socket_value, SOL_SOCKET, SO_REUSEADDR,
                         (const char *)&enabled, (pp_socklen_t)sizeof(enabled));
#if defined(IPV6_V6ONLY)
        if (it->ai_family == AF_INET6) {
            int disabled = 0;
            (void)setsockopt(socket_value, IPPROTO_IPV6, IPV6_V6ONLY,
                             (const char *)&disabled, (pp_socklen_t)sizeof(disabled));
        }
#endif
        if (bind(socket_value, it->ai_addr, (int)it->ai_addrlen) != 0) {
            last_error = pp_net_native_error();
            pp_close_socket(socket_value);
            continue;
        }
        if (!datagram && listen(socket_value, backlog > 0 ? backlog : 16) != 0) {
            last_error = pp_net_native_error();
            pp_close_socket(socket_value);
            continue;
        }
        if (!pp_net_set_nonblocking(socket_value)) {
            last_error = pp_net_native_error();
            pp_close_socket(socket_value);
            continue;
        }
        freeaddrinfo(addresses);
        return pp_net_register(socket_value, datagram);
    }
    freeaddrinfo(addresses);
    pp_net_set_error(datagram ? "UDP bind" : "TCP listen",
                     last_error ? last_error : pp_net_native_error());
    return -1;
}

int64_t pp_net_tcp_listen(const char *host, int64_t port, int64_t backlog) {
    pp_net_reset_status();
    if (!pp_net_available()) { pp_net_set_message(g_net_init_error); return -1; }
    return pp_net_bind_socket(host, port, SOCK_STREAM, IPPROTO_TCP, (int)backlog, 0);
}

int64_t pp_net_udp_bind(const char *host, int64_t port) {
    pp_net_reset_status();
    if (!pp_net_available()) { pp_net_set_message(g_net_init_error); return -1; }
    return pp_net_bind_socket(host, port, SOCK_DGRAM, IPPROTO_UDP, 0, 1);
}

int64_t pp_net_tcp_accept(int64_t listener, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(listener, &socket_value, &datagram)) return -1;
    if (datagram) { pp_net_set_message("cannot accept on a UDP socket"); return -1; }
    if (!pp_net_wait_native(socket_value, 0, timeout_ms)) return -1;
    struct sockaddr_storage address;
    pp_socklen_t length = (pp_socklen_t)sizeof(address);
    pp_native_socket accepted = accept(socket_value, (struct sockaddr *)&address, &length);
    if (accepted == PP_BAD_SOCKET) {
        pp_net_set_error("TCP accept", pp_net_native_error());
        return -1;
    }
    if (!pp_net_set_nonblocking(accepted)) {
        pp_net_set_error("TCP accept", pp_net_native_error());
        pp_close_socket(accepted);
        return -1;
    }
    int enabled = 1;
    (void)setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY,
                     (const char *)&enabled, (pp_socklen_t)sizeof(enabled));
    pp_net_store_address((const struct sockaddr *)&address, length);
    return pp_net_register(accepted, 0);
}

bool pp_net_socket_close(int64_t handle) {
    pp_net_reset_status();
    if (handle <= 0 || handle > PP_NET_MAX_SOCKETS) {
        pp_net_set_message("invalid socket handle");
        return false;
    }
    const int index = (int)handle - 1;
    ppc_plat_mutex_lock(g_net_lock);
    if (!g_sockets[index].used) {
        ppc_plat_mutex_unlock(g_net_lock);
        return true;
    }
    const pp_native_socket socket_value = g_sockets[index].socket;
    g_sockets[index].used = 0;
    ppc_plat_mutex_unlock(g_net_lock);
    if (pp_close_socket(socket_value) != 0) {
        pp_net_set_error("socket close", pp_net_native_error());
        return false;
    }
    return true;
}

bool pp_net_socket_shutdown(int64_t handle, int64_t how) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return false;
    int native_how;
#if PPC_WINDOWS
    native_how = how <= 0 ? SD_RECEIVE : (how == 1 ? SD_SEND : SD_BOTH);
#else
    native_how = how <= 0 ? SHUT_RD : (how == 1 ? SHUT_WR : SHUT_RDWR);
#endif
    if (shutdown(socket_value, native_how) != 0) {
        pp_net_set_error("socket shutdown", pp_net_native_error());
        return false;
    }
    return true;
}

bool pp_net_socket_wait_readable(int64_t handle, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return false;
    return pp_net_wait_native(socket_value, 0, timeout_ms) != 0;
}

bool pp_net_socket_wait_writable(int64_t handle, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return false;
    return pp_net_wait_native(socket_value, 1, timeout_ms) != 0;
}

bool pp_net_socket_set_nodelay(int64_t handle, bool enabled) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(handle, &socket_value, &datagram)) return false;
    if (datagram) { pp_net_set_message("TCP_NODELAY is not valid for UDP"); return false; }
    const int value = enabled ? 1 : 0;
    if (setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY,
                   (const char *)&value, (pp_socklen_t)sizeof(value)) != 0) {
        pp_net_set_error("TCP_NODELAY", pp_net_native_error());
        return false;
    }
    return true;
}

int64_t pp_net_socket_send(int64_t handle, pp_bytes *bytes, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(handle, &socket_value, &datagram)) return -1;
    if (datagram) { pp_net_set_message("use udp_send_to for a UDP socket"); return -1; }
    const int64_t length = pp_bytes_len(bytes);
    if (length <= 0) return 0;
    const uint8_t *data = pp_bytes_data(bytes);
    const int64_t started = ppc_plat_monotonic_ms();
    int64_t sent_total = 0;
    while (sent_total < length) {
        const int64_t remaining = pp_net_remaining(started, timeout_ms);
        if (!pp_net_wait_native(socket_value, 1, remaining)) break;
        const int64_t left = length - sent_total;
        const int chunk = left > 0x3fffffff ? 0x3fffffff : (int)left;
#if PPC_WINDOWS
        const int sent = send(socket_value, (const char *)data + sent_total, chunk, 0);
#else
        const int sent = (int)send(socket_value, data + sent_total, (size_t)chunk, 0);
#endif
        if (sent > 0) { sent_total += sent; continue; }
        if (sent == 0) { g_net_eof = 1; break; }
        const int error = pp_net_native_error();
        if (pp_net_would_block(error)) continue;
        pp_net_set_error("socket send", error);
        break;
    }
    return sent_total;
}

pp_bytes *pp_net_socket_recv(int64_t handle, int64_t max_bytes, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_bytes *out = pp_bytes_new();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(handle, &socket_value, &datagram)) return out;
    if (datagram) { pp_net_set_message("use udp_recv_from for a UDP socket"); return out; }
    if (max_bytes <= 0 || max_bytes > PP_NET_MAX_IO) {
        pp_net_set_message("socket receive size must be 1..64 MiB");
        return out;
    }
    if (!pp_net_wait_native(socket_value, 0, timeout_ms)) return out;
    uint8_t *buffer = (uint8_t *)malloc((size_t)max_bytes);
    if (!buffer) { pp_net_set_message("out of memory receiving socket data"); return out; }
#if PPC_WINDOWS
    const int count = recv(socket_value, (char *)buffer, (int)max_bytes, 0);
#else
    const int count = (int)recv(socket_value, buffer, (size_t)max_bytes, 0);
#endif
    if (count > 0) {
        out = pp_bytes_from_data(buffer, count);
    } else if (count == 0) {
        g_net_eof = 1;
    } else {
        const int error = pp_net_native_error();
        if (!pp_net_would_block(error)) pp_net_set_error("socket receive", error);
        else g_net_timed_out = 1;
    }
    free(buffer);
    return out;
}

int64_t pp_net_udp_send_to(int64_t handle, const char *host, int64_t port,
                           pp_bytes *bytes, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(handle, &socket_value, &datagram)) return -1;
    if (!datagram) { pp_net_set_message("udp_send_to needs a UDP socket"); return -1; }
    if (!host || !*host || port < 1 || port > 65535) {
        pp_net_set_message("UDP destination needs a host and port 1..65535");
        return -1;
    }
    const int64_t length = pp_bytes_len(bytes);
    if (length > 65507) { pp_net_set_message("UDP payload exceeds 65507 bytes"); return -1; }
    char service[16];
    snprintf(service, sizeof(service), "%lld", (long long)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    struct addrinfo *addresses = NULL;
    const int gai = getaddrinfo(host, service, &hints, &addresses);
    if (gai != 0) {
        snprintf(g_net_error, sizeof(g_net_error), "UDP DNS lookup failed (%d)", gai);
        return -1;
    }
    // A host such as localhost commonly resolves to both ::1 and 127.0.0.1.
    // sendto() cannot use an IPv6 sockaddr with an IPv4 socket (or vice versa),
    // so pick an address that matches the socket that udp_bind() actually
    // created instead of blindly taking getaddrinfo()'s first result.
    struct sockaddr_storage local_address;
    pp_socklen_t local_length = (pp_socklen_t)sizeof(local_address);
    int local_family = AF_UNSPEC;
    if (getsockname(socket_value, (struct sockaddr *)&local_address, &local_length) == 0) {
        local_family = ((const struct sockaddr *)&local_address)->sa_family;
    }
    const struct addrinfo *destination = NULL;
    for (const struct addrinfo *candidate = addresses; candidate; candidate = candidate->ai_next) {
        if (local_family == AF_UNSPEC || candidate->ai_family == local_family) {
            destination = candidate;
            break;
        }
    }
    if (!destination) {
        freeaddrinfo(addresses);
        pp_net_set_message("UDP destination has no address matching the socket family");
        return -1;
    }

    int64_t result = -1;
    if (pp_net_wait_native(socket_value, 1, timeout_ms)) {
        const uint8_t *data = pp_bytes_data(bytes);
#if PPC_WINDOWS
        const int sent = sendto(socket_value, (const char *)data, (int)length, 0,
                                destination->ai_addr, (int)destination->ai_addrlen);
#else
        const int sent = (int)sendto(socket_value, data, (size_t)length, 0,
                                     destination->ai_addr, (pp_socklen_t)destination->ai_addrlen);
#endif
        if (sent >= 0) result = sent;
        else pp_net_set_error("UDP send", pp_net_native_error());
    }
    freeaddrinfo(addresses);
    return result;
}

pp_bytes *pp_net_udp_recv_from(int64_t handle, int64_t max_bytes, int64_t timeout_ms) {
    pp_net_reset_status();
    pp_bytes *out = pp_bytes_new();
    pp_native_socket socket_value;
    int datagram = 0;
    if (!pp_net_lookup(handle, &socket_value, &datagram)) return out;
    if (!datagram) { pp_net_set_message("udp_recv_from needs a UDP socket"); return out; }
    if (max_bytes <= 0 || max_bytes > 65507) {
        pp_net_set_message("UDP receive size must be 1..65507 bytes");
        return out;
    }
    if (!pp_net_wait_native(socket_value, 0, timeout_ms)) return out;
    uint8_t *buffer = (uint8_t *)malloc((size_t)max_bytes);
    if (!buffer) { pp_net_set_message("out of memory receiving UDP data"); return out; }
    struct sockaddr_storage address;
    pp_socklen_t length = (pp_socklen_t)sizeof(address);
#if PPC_WINDOWS
    const int count = recvfrom(socket_value, (char *)buffer, (int)max_bytes, 0,
                               (struct sockaddr *)&address, &length);
#else
    const int count = (int)recvfrom(socket_value, buffer, (size_t)max_bytes, 0,
                                    (struct sockaddr *)&address, &length);
#endif
    if (count >= 0) {
        out = pp_bytes_from_data(buffer, count);
        pp_net_store_address((const struct sockaddr *)&address, length);
    } else {
        const int error = pp_net_native_error();
        if (!pp_net_would_block(error)) pp_net_set_error("UDP receive", error);
        else g_net_timed_out = 1;
    }
    free(buffer);
    return out;
}

int64_t pp_net_socket_local_port(int64_t handle) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return 0;
    struct sockaddr_storage address;
    pp_socklen_t length = (pp_socklen_t)sizeof(address);
    if (getsockname(socket_value, (struct sockaddr *)&address, &length) != 0) {
        pp_net_set_error("getsockname", pp_net_native_error());
        return 0;
    }
    pp_net_store_address((const struct sockaddr *)&address, length);
    return g_net_last_port;
}

const char *pp_net_socket_peer_host(int64_t handle) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return "";
    struct sockaddr_storage address;
    pp_socklen_t length = (pp_socklen_t)sizeof(address);
    if (getpeername(socket_value, (struct sockaddr *)&address, &length) != 0) {
        pp_net_set_error("getpeername", pp_net_native_error());
        return "";
    }
    pp_net_store_address((const struct sockaddr *)&address, length);
    const size_t host_length = strlen(g_net_last_host);
    char *copy = (char *)malloc(host_length + 1);
    if (!copy) { pp_net_set_message("out of memory copying peer host"); return ""; }
    memcpy(copy, g_net_last_host, host_length + 1);
    return pp_adopt_text(copy);
}

int64_t pp_net_socket_peer_port(int64_t handle) {
    pp_net_reset_status();
    pp_native_socket socket_value;
    if (!pp_net_lookup(handle, &socket_value, NULL)) return 0;
    struct sockaddr_storage address;
    pp_socklen_t length = (pp_socklen_t)sizeof(address);
    if (getpeername(socket_value, (struct sockaddr *)&address, &length) != 0) {
        pp_net_set_error("getpeername", pp_net_native_error());
        return 0;
    }
    pp_net_store_address((const struct sockaddr *)&address, length);
    return g_net_last_port;
}
