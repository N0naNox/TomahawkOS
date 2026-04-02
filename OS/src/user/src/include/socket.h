/**
 * @file socket.h  (userland)
 * @brief BSD-style Socket API for TomahawkOS user programs.
 *
 * Mirrors the kernel socket.h types and provides thin syscall wrappers
 * so that user-space code can call socket(), bind(), sendto(), etc.
 * without depending on kernel headers.
 */

#pragma once
#include <stdint.h>

/* ====================================================================
 *  Address families
 * ==================================================================== */
#define AF_INET     2

/* ====================================================================
 *  Socket types
 * ==================================================================== */
#define SOCK_DGRAM  2
#define SOCK_STREAM 1
#define SOCK_RAW    3

/* ====================================================================
 *  Protocol numbers
 * ==================================================================== */
#define IPPROTO_UDP  17
#define IPPROTO_TCP   6
#define IPPROTO_ICMP  1

/* ====================================================================
 *  Special addresses
 * ==================================================================== */
#define INADDR_ANY      0x00000000
#define INADDR_LOOPBACK 0x7F000001

/* ====================================================================
 *  Error codes
 * ==================================================================== */
#define SOCK_ERR_OK         0
#define SOCK_ERR_INVAL     -1
#define SOCK_ERR_NOMEM     -2
#define SOCK_ERR_ADDRINUSE -3
#define SOCK_ERR_NOTBOUND  -4
#define SOCK_ERR_AGAIN     -5
#define SOCK_ERR_BADF      -6
#define SOCK_ERR_NOSUPPORT -7
#define SOCK_ERR_MSGSIZE   -8

/* ====================================================================
 *  IPv4 address  (stored in network byte order)
 * ==================================================================== */
typedef struct {
    uint8_t octets[4];
} ipv4_addr_t;

/* Construct an IPv4 address from four dotted-decimal octets (host order) */
static inline ipv4_addr_t IPV4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    ipv4_addr_t addr;
    addr.octets[0] = a;
    addr.octets[1] = b;
    addr.octets[2] = c;
    addr.octets[3] = d;
    return addr;
}

/* Byte-order helpers */
static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8)
         | ((x & 0x0000FF00u) << 8)  | ((x & 0x000000FFu) << 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/* ====================================================================
 *  sockaddr_in — IPv4 socket address
 * ==================================================================== */
typedef struct sockaddr_in {
    uint16_t    sin_family;   /* AF_INET                           */
    uint16_t    sin_port;     /* port (network byte order)         */
    ipv4_addr_t sin_addr;     /* IPv4 address (network byte order) */
    uint8_t     sin_zero[8];  /* padding                           */
} sockaddr_in_t;

/** Build a sockaddr_in from an IPv4 struct + host-byte-order port. */
static inline sockaddr_in_t sockaddr_in_make(ipv4_addr_t ip, uint16_t port)
{
    sockaddr_in_t a;
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr   = ip;
    for (int i = 0; i < 8; i++) a.sin_zero[i] = 0;
    return a;
}

/* ====================================================================
 *  socket_io_args_t — helper struct for sendto() / recvfrom() syscalls.
 *
 *  The syscall ABI passes only three user arguments.  To accommodate
 *  sendto/recvfrom (which each need 4 values), both calls receive a
 *  pointer to this struct as their second argument.
 * ==================================================================== */
typedef struct socket_io_args {
    void        *buf;     /* TX: source buffer;  RX: destination buffer */
    uint16_t     len;     /* TX: bytes to send;  RX: buffer capacity     */
    sockaddr_in_t addr;   /* TX: destination;    RX: filled with sender   */
} socket_io_args_t;

/* ====================================================================
 *  Syscall numbers (must match kernel syscall_numbers.h)
 * ==================================================================== */
#define SYS_SOCKET      80
#define SYS_BIND        81
#define SYS_SENDTO      82
#define SYS_RECVFROM    83
#define SYS_CONNECT     84
#define SYS_SEND        85
#define SYS_RECV        86
#define SYS_SOCK_CLOSE  87

/* ====================================================================
 *  Socket API — userland wrappers
 * ==================================================================== */

/**
 * @brief Create a new socket.
 * @param domain   Address family (AF_INET).
 * @param type     SOCK_DGRAM or SOCK_STREAM.
 * @param protocol 0 (auto), IPPROTO_UDP, or IPPROTO_TCP.
 * @return Non-negative socket fd on success; negative SOCK_ERR_* on failure.
 */
int socket(int domain, int type, int protocol);

/**
 * @brief Bind a socket to a local address and port.
 * @param fd   Socket fd returned by socket().
 * @param addr Pointer to a filled-in sockaddr_in_t.
 * @return 0 on success; negative SOCK_ERR_* on failure.
 */
int bind(int fd, const sockaddr_in_t *addr);

/**
 * @brief Send a datagram to a specific destination.
 * @param fd   Socket fd.
 * @param buf  Payload data.
 * @param len  Payload length (bytes).
 * @param dest Destination address.
 * @return Bytes sent on success; negative SOCK_ERR_* on failure.
 */
int sendto(int fd, const void *buf, uint16_t len, const sockaddr_in_t *dest);

/**
 * @brief Receive a datagram from the socket.
 * @param fd     Socket fd (must be bound first).
 * @param buf    Destination buffer.
 * @param maxlen Maximum bytes to receive.
 * @param from   [out] Sender address (may be NULL).
 * @return Bytes received on success; negative SOCK_ERR_* on failure.
 */
int recvfrom(int fd, void *buf, uint16_t maxlen, sockaddr_in_t *from);

/**
 * @brief Set the default remote address for send().
 * @param fd   Socket fd.
 * @param addr Remote address.
 * @return 0 on success; negative SOCK_ERR_* on failure.
 */
int connect(int fd, const sockaddr_in_t *addr);

/**
 * @brief Send on a connected socket (uses address set by connect()).
 * @param fd  Socket fd.
 * @param buf Payload data.
 * @param len Payload length (bytes).
 * @return Bytes sent on success; negative SOCK_ERR_* on failure.
 */
int send(int fd, const void *buf, uint16_t len);

/**
 * @brief Receive from a connected socket.
 * @param fd     Socket fd.
 * @param buf    Destination buffer.
 * @param maxlen Maximum bytes to receive.
 * @return Bytes received on success; negative SOCK_ERR_* on failure.
 */
int recv(int fd, void *buf, uint16_t maxlen);

/**
 * @brief Close a socket and release its resources.
 * @param fd Socket fd.
 * @return 0 on success; negative SOCK_ERR_* on failure.
 */
int sock_close(int fd);
