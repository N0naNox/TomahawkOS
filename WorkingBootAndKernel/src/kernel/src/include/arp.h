/**
 * @file arp.h
 * @brief ARP — Address Resolution Protocol  (L2½)
 *
 * ARP maps IPv4 addresses to Ethernet MAC addresses so that the IPv4
 * layer can fill in the destination MAC when sending a frame.
 *
 * Responsibilities:
 *  • Maintain an ARP cache (table of IPv4 → MAC mappings)
 *  • Generate ARP Request broadcasts to resolve unknown hosts
 *  • Process incoming ARP Request / Reply and update the cache
 *
 * ─── ARP packet format (28 bytes inside an Ethernet frame) ───
 *
 *  ┌────────┬────────┬──────┬──────┬───────┐
 *  │ htype  │ ptype  │ hlen │ plen │ oper  │  fixed header (8 B)
 *  ├────────┴────────┴──────┴──────┴───────┤
 *  │ sender MAC (6 B)  │ sender IP (4 B)   │  sender addresses
 *  ├───────────────────┼───────────────────┤
 *  │ target MAC (6 B)  │ target IP (4 B)   │  target addresses
 *  └───────────────────┴───────────────────┘
 */

#ifndef ARP_H
#define ARP_H

#include "net.h"

/* Forward declarations */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Constants
 * ==================================================================== */

/** ARP hardware type: Ethernet */
#define ARP_HTYPE_ETHERNET  1

/** ARP protocol type: IPv4 (same value as ETHERTYPE_IPV4) */
#define ARP_PTYPE_IPV4      0x0800

/** ARP operation codes */
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

/** Hardware address length for Ethernet */
#define ARP_HLEN_ETH        6

/** Protocol address length for IPv4 */
#define ARP_PLEN_IPV4       4

/** ARP header length (for Ethernet/IPv4) */
#define ARP_HEADER_LEN      28

/** Maximum entries in the ARP cache */
#define ARP_CACHE_SIZE      32

/** ARP entry states */
#define ARP_STATE_FREE       0   /* slot is unused                      */
#define ARP_STATE_PENDING    1   /* request sent, awaiting reply         */
#define ARP_STATE_RESOLVED   2   /* valid MAC on file                    */

/* ====================================================================
 *  ARP header (Ethernet + IPv4 variant — 28 bytes)
 * ==================================================================== */

typedef struct arp_header {
    uint16_t   htype;        /* hardware type   (ARP_HTYPE_ETHERNET)   */
    uint16_t   ptype;        /* protocol type   (ARP_PTYPE_IPV4)       */
    uint8_t    hlen;         /* hardware addr length (6)               */
    uint8_t    plen;         /* protocol addr length (4)               */
    uint16_t   oper;         /* operation       (REQUEST / REPLY)      */

    /* ---- variable part (for Ethernet + IPv4) ---- */
    mac_addr_t  sender_mac;
    ipv4_addr_t sender_ip;
    mac_addr_t  target_mac;
    ipv4_addr_t target_ip;
} __attribute__((packed)) arp_header_t;

/* ====================================================================
 *  ARP cache entry
 * ==================================================================== */

typedef struct arp_entry {
    ipv4_addr_t ip;
    mac_addr_t  mac;
    int         state;       /* ARP_STATE_* */
    uint32_t    timestamp;   /* tick count when entry was created/updated */
} arp_entry_t;

/* ====================================================================
 *  ARP API
 * ==================================================================== */

/** Initialise the ARP cache.  Called once by net_init(). */
void arp_init(void);

/**
 * @brief Resolve an IPv4 address to a MAC address.
 *
 * Looks up the ARP cache.  If the entry is ARP_STATE_RESOLVED the MAC
 * is written to `*out` and 0 is returned.  Otherwise an ARP request is
 * broadcast and -1 is returned (caller should retry or queue the packet).
 *
 * @param dev  NIC to send the ARP request on (if needed).
 * @param ip   IPv4 address to resolve.
 * @param out  Destination for the resolved MAC.
 * @return     0 if resolved, -1 if pending.
 */
int arp_resolve(struct net_device *dev, ipv4_addr_t ip, mac_addr_t *out);

/**
 * @brief Process an incoming ARP packet (request or reply).
 *
 * Called by `ethernet_receive()` when EtherType == 0x0806.
 * Updates the cache and, for requests aimed at us, sends a reply.
 *
 * @param dev  NIC the packet arrived on.
 * @param nb   Packet buffer; `data` points just past the Ethernet header.
 */
void arp_receive(struct net_device *dev, struct netbuf *nb);

/**
 * @brief Send an ARP request for the given IPv4 address.
 *
 * Builds an ARP request frame and broadcasts it on `dev`.
 */
int arp_send_request(struct net_device *dev, ipv4_addr_t target_ip);

/**
 * @brief Look up a MAC in the cache without sending a request.
 * @return 0 if found (MAC written to *out), -1 if not cached.
 */
int arp_cache_lookup(ipv4_addr_t ip, mac_addr_t *out);

/**
 * @brief Manually insert / update a cache entry.
 */
void arp_cache_update(ipv4_addr_t ip, mac_addr_t mac);

/**
 * @brief Print the ARP cache to the VGA console (debug).
 */
void arp_print_cache(void);

#endif /* ARP_H */
