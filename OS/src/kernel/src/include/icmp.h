/**
 * @file icmp.h
 * @brief ICMP — Internet Control Message Protocol  (L3 control)
 *
 * ICMP rides inside IPv4 (protocol number 1).  It is used for:
 *  • Echo Request / Echo Reply  (ping)
 *  • Destination Unreachable
 *  • Time Exceeded
 *  • (we implement Echo for now; others can be added later)
 *
 * ─── ICMP header (8 bytes minimum) ───
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |     Type      |     Code      |          Checksum             |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          Identifier           |       Sequence Number         |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                          Data ...                             |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#ifndef ICMP_H
#define ICMP_H

#include "net.h"

/* Forward declarations */
struct net_device;
struct netbuf;

/* ====================================================================
 *  ICMP type codes
 * ==================================================================== */

#define ICMP_TYPE_ECHO_REPLY       0
#define ICMP_TYPE_DEST_UNREACH     3
#define ICMP_TYPE_ECHO_REQUEST     8
#define ICMP_TYPE_TIME_EXCEEDED   11

/* ---- Code values for ICMP_TYPE_DEST_UNREACH ---- */
#define ICMP_CODE_NET_UNREACH      0
#define ICMP_CODE_HOST_UNREACH     1
#define ICMP_CODE_PROTO_UNREACH    2
#define ICMP_CODE_PORT_UNREACH     3

/** ICMP header length (type + code + checksum + id + seq = 8 bytes) */
#define ICMP_HEADER_LEN  8

/* ====================================================================
 *  ICMP header
 * ==================================================================== */

typedef struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;

    /* ---- Rest of header depends on type.  For echo it is: ---- */
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

/* ====================================================================
 *  ICMP API
 * ==================================================================== */

/**
 * @brief Process an incoming ICMP message.
 *
 * Called by `ipv4_receive()` when protocol == 1.
 * For Echo Requests we automatically send an Echo Reply (ping).
 *
 * @param dev  NIC the packet arrived on.
 * @param nb   Packet buffer; `data` points to the ICMP header.
 * @param src  Source IPv4 of the sender (for the reply).
 */
void icmp_receive(struct net_device *dev, struct netbuf *nb,
                  ipv4_addr_t src);

/**
 * @brief Send an ICMP Echo Request (ping) to `dst`.
 *
 * @param dev   NIC to send on.
 * @param dst   Destination IPv4 address.
 * @param id    Identifier (usually PID or a sequence counter).
 * @param seq   Sequence number.
 * @param data  Optional payload bytes (can be NULL).
 * @param data_len  Length of the payload.
 * @return 0 on success, negative on error.
 */
int icmp_send_echo_request(struct net_device *dev,
                           ipv4_addr_t dst,
                           uint16_t id,
                           uint16_t seq,
                           const void *data,
                           uint16_t data_len);

#endif /* ICMP_H */
