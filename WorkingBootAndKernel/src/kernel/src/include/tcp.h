#pragma once
/**
 * @file tcp.h
 * @brief Minimal synchronous TCP client (poll-mode, single connection)
 *
 * Supports only the operations needed for an HTTP client:
 *   1. tcp_connect()          — three-way handshake
 *   2. tcp_write()            — send a request
 *   3. tcp_recv_until_close() — read all data until server FIN
 *   4. tcp_close()            — send FIN
 *
 * Only one connection is active at a time.  It is managed via a
 * tcp_conn_t the caller allocates on its stack.
 */

#include "net.h"
#include "net_device.h"
#include <stdint.h>

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** Max TCP connection receive buffer (truncates at this size) */
#define TCP_RECV_BUF_SIZE    8192

/** Ephemeral source port used for outgoing connections */
#define TCP_EPHEMERAL_PORT   49300u

/** Window size advertised to the remote peer */
#define TCP_WINDOW_SIZE      8192u

/** Initial sequence number */
#define TCP_ISN              0xC0DE0000u

/* ---- TCP flags ---- */
#define TCP_FLAG_FIN  0x01u
#define TCP_FLAG_SYN  0x02u
#define TCP_FLAG_RST  0x04u
#define TCP_FLAG_PSH  0x08u
#define TCP_FLAG_ACK  0x10u

/* ====================================================================
 *  TCP header (20 bytes, no options)
 * ==================================================================== */

typedef struct __attribute__((packed)) {
    uint16_t src_port;   /* source port (network byte order)      */
    uint16_t dst_port;   /* destination port (network byte order) */
    uint32_t seq;        /* sequence number (NBO)                 */
    uint32_t ack_seq;    /* acknowledgement number (NBO)          */
    uint8_t  data_off;   /* (header_len/4) << 4                  */
    uint8_t  flags;      /* TCP_FLAG_* bitmask                   */
    uint16_t window;     /* receive window (NBO)                  */
    uint16_t checksum;   /* checksum (NBO)                        */
    uint16_t urg_ptr;    /* urgent pointer (NBO, unused)          */
} tcp_header_t;

#define TCP_HEADER_LEN  20u   /* header with no options */

/* ====================================================================
 *  Connection state
 * ==================================================================== */

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT,
    TCP_STATE_TIME_WAIT,
} tcp_state_t;

typedef struct {
    /* addressing */
    net_device_t *dev;
    ipv4_addr_t   remote_ip;
    uint16_t      local_port;
    uint16_t      remote_port;

    /* sequence tracking */
    uint32_t      snd_next;   /* next SEQ we will send             */
    uint32_t      rcv_next;   /* next SEQ we expect from peer      */

    /* state */
    tcp_state_t   state;
    int           peer_fin;   /* non-zero once remote sent FIN     */

    /* receive buffer (filled by tcp_receive) */
    uint8_t       recv_buf[TCP_RECV_BUF_SIZE];
    uint32_t      recv_len;
} tcp_conn_t;

/* ====================================================================
 *  Public API
 * ==================================================================== */

/** Initialise the TCP layer (clear active-connection pointer). */
void tcp_init(void);

/**
 * @brief Perform the TCP three-way handshake.
 *
 * Sends SYN, polls until SYN-ACK arrives, sends ACK.
 *
 * @param conn      Caller-allocated connection struct (zeroed by this function).
 * @param dev       NIC to use.
 * @param dst_ip    Remote IPv4 address.
 * @param dst_port  Remote TCP port (host byte order).
 * @return 0 on success, -1 on timeout.
 */
int tcp_connect(tcp_conn_t *conn, net_device_t *dev,
                ipv4_addr_t dst_ip, uint16_t dst_port);

/**
 * @brief Send data on an ESTABLISHED connection.
 * @return 0 on success, -1 on error.
 */
int tcp_write(tcp_conn_t *conn, const uint8_t *data, uint16_t len);

/**
 * @brief Poll until the remote peer closes the connection.
 *
 * Accumulates all received payload into conn->recv_buf (up to
 * TCP_RECV_BUF_SIZE bytes).  Returns when peer sends FIN or on timeout.
 *
 * @return Number of bytes received, or -1 on error.
 */
int tcp_recv_until_close(tcp_conn_t *conn);

/**
 * @brief Send FIN and free the connection slot.
 */
void tcp_close(tcp_conn_t *conn);

/**
 * @brief Process a received TCP segment.
 *
 * Called by ipv4_receive() when protocol == IP_PROTO_TCP.
 * Updates the active connection's state and receive buffer.
 */
void tcp_receive(struct net_device *dev, struct netbuf *nb,
                 ipv4_addr_t src_ip, ipv4_addr_t dst_ip);
