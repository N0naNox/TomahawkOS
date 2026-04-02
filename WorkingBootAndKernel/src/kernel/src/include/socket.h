/**
 * @file socket.h
 * @brief BSD-style Socket Data Structures & API
 *
 * Provides the standard socket abstraction for TomahawkOS.
 * Sockets are the userspace-facing interface to the network stack,
 * sitting above the transport layer (UDP/TCP) and below the
 * application layer.
 *
 * ══════════════════════════════════════════════════════════════
 *  SOCKET ARCHITECTURE
 * ══════════════════════════════════════════════════════════════
 *
 *   Application / Shell
 *       │  socket(), bind(), sendto(), recvfrom(), close()
 *       ▼
 *   ┌──────────────────────────────────┐
 *   │  Socket Layer                    │
 *   │  - socket_t descriptor table     │
 *   │  - per-socket receive ring buf   │
 *   │  - sockaddr_in addressing        │
 *   │  - protocol dispatch             │
 *   └──────────┬───────────────────────┘
 *              │
 *       ┌──────┴──────┐
 *       ▼             ▼
 *     UDP           TCP (future)
 *       │             │
 *       ▼             ▼
 *     IPv4          IPv4
 *       │
 *       ▼
 *   Ethernet → NIC
 *
 * ── Data flow ──
 *
 *   TX:  sendto() → socket layer → udp_send() → ipv4 → ethernet → NIC
 *
 *   RX:  NIC → ethernet → ipv4 → udp_receive() → socket RX ring
 *        → recvfrom() returns data to caller
 *
 * ══════════════════════════════════════════════════════════════
 */

#ifndef SOCKET_H
#define SOCKET_H

#include "net.h"
#include "net_device.h"

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** Maximum number of open sockets system-wide */
#define SOCKET_MAX          32

/** Per-socket receive ring buffer capacity (number of datagrams) */
#define SOCKET_RX_RING_SIZE 16

/** Maximum payload size stored in the receive ring (bytes per entry) */
#define SOCKET_RX_BUF_SIZE  1500

/* ---- Address families ---- */

#define AF_INET     2       /* IPv4 */

/* ---- Socket types ---- */

#define SOCK_DGRAM  2       /* UDP — connectionless datagram     */
#define SOCK_STREAM 1       /* TCP — connection-oriented stream  */
#define SOCK_RAW    3       /* Raw IP (future)                   */

/* ---- Protocol numbers (match IP protocol field) ---- */

#define IPPROTO_UDP  17
#define IPPROTO_TCP   6
#define IPPROTO_ICMP  1

/* ---- Special values ---- */

#define INADDR_ANY       0x00000000   /* 0.0.0.0       */
#define INADDR_LOOPBACK  0x7F000001   /* 127.0.0.1 (host byte order) */

/* ---- Socket states ---- */

#define SOCK_STATE_FREE      0   /* slot not in use                    */
#define SOCK_STATE_CREATED   1   /* socket() called, not yet bound     */
#define SOCK_STATE_BOUND     2   /* bind() called, receiving enabled   */
#define SOCK_STATE_CONNECTED 3   /* connect() called (UDP: default dst)*/
#define SOCK_STATE_LISTENING 4   /* listen() called (TCP, future)      */
#define SOCK_STATE_CLOSED    5   /* close() called, pending cleanup    */

/* ---- Error codes ---- */

#define SOCK_ERR_OK         0
#define SOCK_ERR_INVAL     -1   /* invalid argument                   */
#define SOCK_ERR_NOMEM     -2   /* out of memory / table full         */
#define SOCK_ERR_ADDRINUSE -3   /* port already bound                 */
#define SOCK_ERR_NOTBOUND  -4   /* operation requires bind first      */
#define SOCK_ERR_AGAIN     -5   /* no data available (non-blocking)   */
#define SOCK_ERR_BADF      -6   /* bad socket descriptor              */
#define SOCK_ERR_NOSUPPORT -7   /* protocol/type not supported        */
#define SOCK_ERR_MSGSIZE   -8   /* message too large                  */

/* ====================================================================
 *  Address structures  (BSD-compatible layout)
 * ==================================================================== */

/**
 * @brief Generic socket address (protocol-independent wrapper).
 */
typedef struct sockaddr {
    uint16_t sa_family;         /* address family (AF_INET, …)        */
    uint8_t  sa_data[14];       /* protocol-specific address data     */
} sockaddr_t;

/**
 * @brief IPv4 socket address.
 *
 * This is the primary address structure passed to bind(), sendto(),
 * recvfrom(), connect().
 *
 *   struct sockaddr_in {
 *       .sin_family = AF_INET;
 *       .sin_port   = htons(port);     // network byte order
 *       .sin_addr   = IPV4(a,b,c,d);   // network byte order
 *   };
 */
typedef struct sockaddr_in {
    uint16_t    sin_family;     /* AF_INET                            */
    uint16_t    sin_port;       /* port number (network byte order)   */
    ipv4_addr_t sin_addr;       /* IPv4 address (network byte order)  */
    uint8_t     sin_zero[8];    /* padding to match sockaddr size     */
} sockaddr_in_t;

/* ====================================================================
 *  Receive ring buffer — queues incoming datagrams per socket
 * ==================================================================== */

/**
 * @brief One entry in the per-socket receive ring.
 *
 * Each datagram received is stored with its source address
 * so that recvfrom() can return the sender's endpoint.
 */
typedef struct socket_rx_entry {
    uint8_t     data[SOCKET_RX_BUF_SIZE]; /* payload copy             */
    uint16_t    data_len;                 /* actual bytes stored       */
    sockaddr_in_t from;                   /* source address            */
    int         valid;                    /* non-zero if entry is used */
} socket_rx_entry_t;

/**
 * @brief Per-socket receive ring buffer.
 *
 * A simple fixed-size circular buffer of datagrams.
 *   - Producer: the internal UDP/TCP receive callback (ISR-safe)
 *   - Consumer: recvfrom() called from kernel or userspace
 *
 * head = next slot to write (producer index)
 * tail = next slot to read  (consumer index)
 * count = number of valid entries
 */
typedef struct socket_rx_ring {
    socket_rx_entry_t entries[SOCKET_RX_RING_SIZE];
    int head;       /* write index  (0 .. SOCKET_RX_RING_SIZE-1) */
    int tail;       /* read index   (0 .. SOCKET_RX_RING_SIZE-1) */
    int count;      /* entries currently queued                   */
} socket_rx_ring_t;

/* ====================================================================
 *  socket_t — the core socket descriptor
 * ==================================================================== */

/**
 * @brief A socket descriptor.
 *
 * Each open socket occupies one slot in the global socket table.
 * The socket "file descriptor" returned to the caller is simply the
 * index into that table (0 .. SOCKET_MAX-1).
 *
 * Key fields:
 *   domain   — address family (AF_INET)
 *   type     — SOCK_DGRAM / SOCK_STREAM / SOCK_RAW
 *   protocol — IPPROTO_UDP / IPPROTO_TCP / 0 (auto)
 *   state    — lifecycle (FREE → CREATED → BOUND → …)
 *   local    — bound local address:port
 *   remote   — connected remote address:port (connect / accept)
 *   rx_ring  — incoming datagram queue
 *   dev      — NIC to route through (NULL = auto-select)
 */
typedef struct socket {
    /* ---- identity ---- */
    int             fd;         /* index in the global socket table    */
    int             domain;     /* AF_INET                             */
    int             type;       /* SOCK_DGRAM, SOCK_STREAM             */
    int             protocol;   /* IPPROTO_UDP, IPPROTO_TCP, etc.      */
    int             state;      /* SOCK_STATE_*                        */

    /* ---- addressing ---- */
    sockaddr_in_t   local;      /* our side  (populated by bind)       */
    sockaddr_in_t   remote;     /* peer side (populated by connect)    */

    /* ---- receive queue ---- */
    socket_rx_ring_t rx_ring;

    /* ---- options / flags ---- */
    int             non_blocking;  /* if set, recvfrom returns EAGAIN  */
    int             broadcast;     /* SO_BROADCAST — allow 255.255.255.255 */
    int             reuse_addr;    /* SO_REUSEADDR                     */

    /* ---- NIC binding ---- */
    net_device_t   *dev;        /* bound device (NULL = default route) */
} socket_t;

/* ====================================================================
 *  Socket API  (BSD-like)
 * ==================================================================== */

/** Initialise the socket table.  Called once by net_init(). */
void socket_init(void);

/**
 * @brief Create a new socket.
 *
 * @param domain    Address family (AF_INET).
 * @param type      Socket type   (SOCK_DGRAM for UDP).
 * @param protocol  Protocol      (0 = auto, or IPPROTO_UDP / IPPROTO_TCP).
 * @return Non-negative socket fd on success, negative SOCK_ERR_* on failure.
 */
int sock_create(int domain, int type, int protocol);

/**
 * @brief Bind a socket to a local address and port.
 *
 * @param fd    Socket descriptor.
 * @param addr  Local address (sockaddr_in_t*).
 * @return 0 on success, negative SOCK_ERR_* on failure.
 */
int sock_bind(int fd, const sockaddr_in_t *addr);

/**
 * @brief Send a datagram to a specified destination.
 *
 * For SOCK_DGRAM sockets.  The destination is given in `dest`.
 *
 * @param fd       Socket descriptor.
 * @param buf      Payload data.
 * @param len      Payload length.
 * @param dest     Destination address.
 * @return Number of bytes sent, or negative SOCK_ERR_*.
 */
int sock_sendto(int fd, const void *buf, uint16_t len,
                const sockaddr_in_t *dest);

/**
 * @brief Receive a datagram from the socket's RX ring.
 *
 * For SOCK_DGRAM sockets.  If `from` is non-NULL, it is filled
 * with the source address.
 *
 * Blocking: spins until data is available (unless non_blocking is set).
 *
 * @param fd       Socket descriptor.
 * @param buf      Destination buffer for the received data.
 * @param maxlen   Maximum bytes to copy into buf.
 * @param from     [out] Source address (may be NULL).
 * @return Number of bytes received, or negative SOCK_ERR_*.
 */
int sock_recvfrom(int fd, void *buf, uint16_t maxlen,
                  sockaddr_in_t *from);

/**
 * @brief Set a default remote address (for send() without sendto()).
 *
 * After connect(), sock_send() can be used without specifying
 * a destination every time.  For UDP this is purely a convenience.
 *
 * @param fd    Socket descriptor.
 * @param addr  Remote address.
 * @return 0 on success, negative SOCK_ERR_*.
 */
int sock_connect(int fd, const sockaddr_in_t *addr);

/**
 * @brief Send data on a connected socket (default destination).
 *
 * Equivalent to sendto() using the address set by connect().
 *
 * @param fd   Socket descriptor.
 * @param buf  Payload data.
 * @param len  Payload length.
 * @return Number of bytes sent, or negative SOCK_ERR_*.
 */
int sock_send(int fd, const void *buf, uint16_t len);

/**
 * @brief Receive data from a connected socket.
 *
 * Like recvfrom() but does not return the source address.
 *
 * @param fd      Socket descriptor.
 * @param buf     Destination buffer.
 * @param maxlen  Maximum bytes to copy.
 * @return Number of bytes received, or negative SOCK_ERR_*.
 */
int sock_recv(int fd, void *buf, uint16_t maxlen);

/**
 * @brief Close a socket and release its resources.
 *
 * Unbinds the port, drains the RX ring, and returns the slot
 * to the free pool.
 *
 * @param fd  Socket descriptor.
 * @return 0 on success, negative SOCK_ERR_*.
 */
int sock_close(int fd);

/* ====================================================================
 *  Syscall helper — packs the extra arguments for sendto / recvfrom
 *  since the syscall ABI only carries three user-visible registers.
 * ==================================================================== */

/**
 * @brief Argument block passed by pointer for SYS_SENDTO / SYS_RECVFROM.
 *
 * Userspace fills this struct and passes its address as arg2:
 *
 *   SYS_SENDTO:
 *     arg1 = fd
 *     arg2 = pointer to socket_io_args_t  { buf (const void*), len, addr (dest) }
 *
 *   SYS_RECVFROM:
 *     arg1 = fd
 *     arg2 = pointer to socket_io_args_t  { buf (void*), len (maxlen), addr (filled with sender) }
 */
typedef struct socket_io_args {
    void        *buf;       /* TX: payload source;  RX: payload destination   */
    uint16_t     len;       /* TX: bytes to send;   RX: buffer capacity        */
    sockaddr_in_t addr;     /* TX: destination;     RX: filled with source      */
} socket_io_args_t;

/* ---- Utility ---- */

/**
 * @brief Get a pointer to a socket by fd (bounds-checked).
 * @return Pointer, or NULL if fd is invalid or slot is FREE.
 */
socket_t *socket_get(int fd);

/**
 * @brief Print the socket table to serial (debug).
 */
void socket_print_table(void);

/**
 * @brief Build a sockaddr_in from an IPv4 + port (convenience).
 *
 * @param ip    IPv4 address (network byte order struct).
 * @param port  Port number in **host** byte order.
 */
static inline sockaddr_in_t sockaddr_in_make(ipv4_addr_t ip, uint16_t port)
{
    sockaddr_in_t a;
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr   = ip;
    for (int i = 0; i < 8; i++) a.sin_zero[i] = 0;
    return a;
}

#endif /* SOCKET_H */
