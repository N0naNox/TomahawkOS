/**
 * @file udp.h
 * @brief UDP — User Datagram Protocol  (L4)
 *
 * Connectionless, unreliable datagram transport.  This is the simplest
 * L4 protocol and the first one we implement because:
 *  • DNS uses UDP (port 53)
 *  • DHCP uses UDP (ports 67/68)
 *  • It has no state machine, no retransmission — easy to get right
 *
 * ─── UDP header (8 bytes) ───
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          Source Port          |       Destination Port        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |            Length             |           Checksum            |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                          Data ...                             |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#ifndef UDP_H
#define UDP_H

#include "net.h"

/* Forward declarations */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** UDP header length in bytes */
#define UDP_HEADER_LEN  8

/** Maximum number of bound UDP ports we track */
#define UDP_MAX_BINDS   16

/* ---- Well-known ports ---- */

#define UDP_PORT_DNS     53
#define UDP_PORT_DHCP_S  67     /* DHCP server port */
#define UDP_PORT_DHCP_C  68     /* DHCP client port */

/* ====================================================================
 *  UDP header
 * ==================================================================== */

typedef struct udp_header {
    uint16_t src_port;       /* source port (big-endian)            */
    uint16_t dst_port;       /* destination port (big-endian)       */
    uint16_t length;         /* header + payload in bytes (BE)      */
    uint16_t checksum;       /* optional (0 = not computed)         */
} __attribute__((packed)) udp_header_t;

/**
 * @brief Pseudo-header used for UDP checksum computation.
 *
 * The UDP checksum covers this 12-byte pseudo-header, then the
 * UDP header itself, then the payload.
 */
typedef struct udp_pseudo_header {
    ipv4_addr_t src;
    ipv4_addr_t dst;
    uint8_t     zero;
    uint8_t     protocol;    /* always 17 */
    uint16_t    udp_length;
} __attribute__((packed)) udp_pseudo_header_t;

/* ====================================================================
 *  UDP receive callback
 * ==================================================================== */

/**
 * @brief Signature for a per-port UDP receive handler.
 *
 * When a datagram arrives on a bound port, the stack calls the
 * registered callback with the payload data and source info.
 */
typedef void (*udp_recv_callback_t)(struct net_device *dev,
                                    ipv4_addr_t src_ip,
                                    uint16_t src_port,
                                    const uint8_t *data,
                                    uint16_t data_len);

/* ====================================================================
 *  UDP API
 * ==================================================================== */

/** Initialise the UDP layer (clear bind table).  Called by net_init(). */
void udp_init(void);

/**
 * @brief Bind a callback to a local UDP port.
 *
 * When a datagram arrives whose destination port matches `port`,
 * `cb` is invoked with the payload.
 *
 * @param port  Local port number (host byte order).
 * @param cb    Callback function.
 * @return  0 on success, -1 if bind table full or port already bound.
 */
int udp_bind(uint16_t port, udp_recv_callback_t cb);

/**
 * @brief Unbind a previously bound UDP port.
 * @return 0 on success, -1 if the port was not bound.
 */
int udp_unbind(uint16_t port);

/**
 * @brief Send a UDP datagram.
 *
 * Builds the 8-byte UDP header, computes the checksum, then passes
 * the packet to `ipv4_send()` with protocol=17.
 *
 * @param dev       NIC to send on.
 * @param dst_ip    Destination IPv4 address.
 * @param src_port  Source port (host byte order).
 * @param dst_port  Destination port (host byte order).
 * @param data      Payload bytes.
 * @param data_len  Payload length.
 * @return 0 on success, negative on error.
 */
int udp_send(struct net_device *dev,
             ipv4_addr_t dst_ip,
             uint16_t src_port,
             uint16_t dst_port,
             const void *data,
             uint16_t data_len);

/**
 * @brief Process an incoming UDP datagram.
 *
 * Called by `ipv4_receive()` when protocol == 17.
 * Validates the header, then dispatches to the registered callback
 * for the destination port (if any).
 *
 * @param dev  NIC the packet arrived on.
 * @param nb   Packet buffer; `data` points to the UDP header.
 * @param src  Source IPv4 address (from the IP layer).
 * @param dst  Destination IPv4 address (from the IP layer).
 */
void udp_receive(struct net_device *dev, struct netbuf *nb,
                 ipv4_addr_t src, ipv4_addr_t dst);

#endif /* UDP_H */
