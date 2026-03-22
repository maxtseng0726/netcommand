#pragma once
// ─────────────────────────────────────────────
//  nc_socket.h  —  thin cross-platform socket layer
//  Wraps Winsock2 (Windows) and POSIX sockets (macOS/Linux)
//  into a unified API used by both admin and client.
// ─────────────────────────────────────────────

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET nc_sock_t;
#  define NC_INVALID_SOCK  INVALID_SOCKET
#  define nc_close(s)      closesocket(s)
#  define nc_errno()       WSAGetLastError()
#  define NC_EAGAIN        WSAEWOULDBLOCK
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef int nc_sock_t;
#  define NC_INVALID_SOCK  (-1)
#  define nc_close(s)      close(s)
#  define nc_errno()       errno
#  define NC_EAGAIN        EAGAIN
#endif

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise (call once at startup; no-op on POSIX)
static inline bool nc_net_init(void)
{
#ifdef _WIN32
    WSADATA wd;
    return WSAStartup(MAKEWORD(2,2), &wd) == 0;
#else
    return true;
#endif
}

// Cleanup (call once at exit; no-op on POSIX)
static inline void nc_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// Set TCP_NODELAY (disable Nagle — important for low-latency events)
static inline void nc_set_nodelay(nc_sock_t s)
{
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
}

// Set socket receive/send timeout in milliseconds
static inline void nc_set_timeout(nc_sock_t s, int ms)
{
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

// Blocking send — retries until all bytes sent or error
static inline int nc_send_all(nc_sock_t s, const void* buf, int len)
{
    const char* p = (const char*)buf;
    int remaining = len;
    while (remaining > 0) {
        int sent = (int)send(s, p, remaining, 0);
        if (sent <= 0) return -1;
        p         += sent;
        remaining -= sent;
    }
    return len;
}

// Blocking recv — retries until all bytes received or error/EOF
static inline int nc_recv_all(nc_sock_t s, void* buf, int len)
{
    char* p = (char*)buf;
    int remaining = len;
    while (remaining > 0) {
        int got = (int)recv(s, p, remaining, 0);
        if (got <= 0) return -1;   // 0 = orderly shutdown, <0 = error
        p         += got;
        remaining -= got;
    }
    return len;
}

// Send a complete NetCommand packet (header + payload)
static inline int nc_send_packet(nc_sock_t s, NC_Command cmd,
                                  const void* payload, uint32_t payload_len,
                                  uint32_t seq)
{
    NC_Header hdr;
    nc_fill_header(&hdr, cmd, payload_len, seq);

    if (nc_send_all(s, &hdr, (int)NC_HEADER_SIZE) < 0) return -1;
    if (payload_len > 0)
        if (nc_send_all(s, payload, (int)payload_len) < 0) return -1;
    return 0;
}

// Receive a header; returns false on error/disconnect
static inline bool nc_recv_header(nc_sock_t s, NC_Header* out_hdr)
{
    if (nc_recv_all(s, out_hdr, (int)NC_HEADER_SIZE) < 0) return false;
    // Fix byte order
    out_hdr->magic  = ntohs(out_hdr->magic);
    out_hdr->length = ntohl(out_hdr->length);
    out_hdr->seq    = ntohl(out_hdr->seq);
    return nc_check_header(out_hdr);
}

#ifdef __cplusplus
}
#endif
