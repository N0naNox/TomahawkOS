/**
 * @file ethernet.h
 * @brief Ethernet (L2) — frame format, EtherType constants, TX/RX API
 *
 * The Ethernet layer is responsible for:
 *  • Constructing outgoing frames (prepend src/dst MAC + EtherType)
 *  • Parsing incoming frames and demultiplexing by EtherType:
 *      0x0806  →  ARP
 *      0x0800  →  IPv4
 *
 * All multi-byte fields are in **network byte order** (big-endian).
 */

#ifndef ETHERNET_H
#define ETHERNET_H

#include "net.h"

/* Forward declaration */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** Standard Ethernet MTU (maximum payload without 802.1Q tag) */
#define ETH_MTU         1500

/** Ethernet header size: 6 dst + 6 src + 2 ethertype = 14 bytes */
#define ETH_HEADER_LEN  14

/** Minimum Ethernet frame payload (without header) */
#define ETH_MIN_PAYLOAD 46

/** Maximum Ethernet frame size on the wire (header + payload, no CRC) */
#define ETH_FRAME_MAX   (ETH_HEADER_LEN + ETH_MTU)

/* ---- EtherType values (network byte order handled by callers) ---- */

#define ETHERTYPE_IPV4  0x0800   /* Internet Protocol version 4 */
#define ETHERTYPE_ARP   0x0806   /* Address Resolution Protocol */
#define ETHERTYPE_IPV6  0x86DD   /* Internet Protocol version 6 (future) */

/* ====================================================================
 *  Ethernet header
 * ==================================================================== */

/**
 * @brief On-wire Ethernet II frame header.
 *
 *  ┌────────────────┬────────────────┬───────────┐
 *  │ dst MAC (6 B)  │ src MAC (6 B)  │ type (2B) │
 *  └────────────────┴────────────────┴───────────┘
 *                                     ↑ EtherType
 */
typedef struct eth_header {
    mac_addr_t dst;          /* destination MAC address           */
    mac_addr_t src;          /* source MAC address                */
    uint16_t   ethertype;    /* protocol type (big-endian)        */
} __attribute__((packed)) eth_header_t;

/* ====================================================================
 *  Ethernet API
 * ==================================================================== */

/**
 * @brief Send an Ethernet frame.
 *
 * Prepends the 14-byte Ethernet header to the data already in `nb`,
 * then hands the frame to the NIC driver via `dev->ops->send()`.
 *
 * The caller should have built the L3+ payload in `nb` starting at
 * `nb->data`.  This function will call `netbuf_push(nb, ETH_HEADER_LEN)`
 * to add the Ethernet header in front.
 *
 * @param dev       NIC to transmit on.
 * @param nb        Packet buffer with L3 payload already in place.
 * @param dst       Destination MAC address.
 * @param ethertype EtherType value in **host** byte order (e.g. 0x0800).
 * @return 0 on success, negative on error.
 */
int ethernet_send_frame(struct net_device *dev,
                        struct netbuf *nb,
                        mac_addr_t dst,
                        uint16_t ethertype);

/**
 * @brief Process a received Ethernet frame.
 *
 * Called by the NIC driver (or the poll loop) after placing a complete
 * Ethernet frame into `nb`.  This function:
 *   1. Validates the header
 *   2. Records the mac_offset in `nb`
 *   3. Strips the Ethernet header (`netbuf_pull`)
 *   4. Dispatches to the right L3 handler based on EtherType:
 *        ETHERTYPE_ARP  → arp_receive()
 *        ETHERTYPE_IPV4 → ipv4_receive()
 *
 * @param dev  NIC the frame arrived on.
 * @param nb   Packet buffer containing the raw Ethernet frame.
 */
void ethernet_receive(struct net_device *dev, struct netbuf *nb);

#endif /* ETHERNET_H */
