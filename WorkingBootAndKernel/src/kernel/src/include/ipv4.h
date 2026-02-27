/**
 * @file ipv4.h
 * @brief IPv4 — Internet Protocol version 4  (L3)
 *
 * The IP layer is responsible for:
 *  • Building outgoing IPv4 datagrams (header + upper-layer payload)
 *  • Validating incoming datagrams (version, checksum, length)
 *  • Demultiplexing by protocol number to the right L4 handler:
 *        1  →  ICMP
 *       17  →  UDP
 *        6  →  TCP  (future)
 *  • (future) Routing decisions / fragmentation / TTL decrement
 *
 * ─── IPv4 header (20 bytes minimum, no options) ───
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |Version|  IHL  |    DSCP   |ECN|         Total Length          |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |         Identification        |Flags|     Fragment Offset     |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |  Time to Live |    Protocol   |       Header Checksum         |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                      Source IP Address                        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                   Destination IP Address                      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#ifndef IPV4_H
#define IPV4_H

#include "net.h"

/* Forward declarations */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** IP version we support */
#define IPV4_VERSION        4

/** Minimum header length in 32-bit words (no options) */
#define IPV4_IHL_MIN        5

/** Minimum header length in bytes */
#define IPV4_HEADER_LEN     20

/** Default Time-To-Live */
#define IPV4_DEFAULT_TTL    64

/* ---- Protocol numbers (go in the `protocol` field) ---- */

#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP       17

/* ---- IP flags (3 bits in the flags/fragment-offset word) ---- */

#define IPV4_FLAG_DF  0x4000   /* Don't Fragment               */
#define IPV4_FLAG_MF  0x2000   /* More Fragments               */

/* ====================================================================
 *  IPv4 header
 * ==================================================================== */

typedef struct ipv4_header {
    uint8_t    version_ihl;     /* version (4 bits) | IHL (4 bits)    */
    uint8_t    dscp_ecn;        /* DSCP (6 bits) | ECN (2 bits)       */
    uint16_t   total_length;    /* header + payload in bytes (BE)     */
    uint16_t   identification;  /* fragment grouping                  */
    uint16_t   flags_fragoff;   /* flags (3 bits) | frag offset (13)  */
    uint8_t    ttl;             /* time to live                       */
    uint8_t    protocol;        /* upper-layer protocol (ICMP/UDP/TCP)*/
    uint16_t   checksum;        /* header checksum                    */
    ipv4_addr_t src;            /* source address                     */
    ipv4_addr_t dst;            /* destination address                */
} __attribute__((packed)) ipv4_header_t;

/* ---- helper macros for version_ihl ---- */
#define IPV4_GET_VERSION(h) (((h)->version_ihl >> 4) & 0x0F)
#define IPV4_GET_IHL(h)     ((h)->version_ihl & 0x0F)
#define IPV4_MAKE_VER_IHL(v, ihl) (((v) << 4) | ((ihl) & 0x0F))

/* ====================================================================
 *  IPv4 API
 * ==================================================================== */

/**
 * @brief Send an IPv4 datagram.
 *
 * Builds the 20-byte IPv4 header, computes the header checksum,
 * then hands the packet to the Ethernet layer (via ARP for the
 * destination MAC resolution).
 *
 * `nb` should already contain the L4 payload (e.g. UDP header + data)
 * starting at `nb->data`.  This function will `netbuf_push()` the
 * 20-byte IP header in front.
 *
 * @param dev      NIC to send on.
 * @param nb       Packet with L4 payload already placed.
 * @param dst      Destination IPv4 address.
 * @param protocol IP protocol number (IP_PROTO_ICMP, etc.).
 * @return 0 on success, negative on error.
 */
int ipv4_send(struct net_device *dev,
              struct netbuf *nb,
              ipv4_addr_t dst,
              uint8_t protocol);

/**
 * @brief Process a received IPv4 datagram.
 *
 * Called by `ethernet_receive()` when EtherType == 0x0800.
 * Validates the header, then dispatches to the correct L4 handler:
 *   protocol 1   → icmp_receive()
 *   protocol 17  → udp_receive()
 *   protocol 6   → tcp_receive() (future)
 *
 * @param dev  NIC the packet arrived on.
 * @param nb   Packet buffer; `data` points to the IPv4 header.
 */
void ipv4_receive(struct net_device *dev, struct netbuf *nb);

#endif /* IPV4_H */
