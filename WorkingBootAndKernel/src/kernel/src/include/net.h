/**
 * @file net.h
 * @brief Core Network Stack — types, packet buffers, subsystem init
 *
 * This is the central header for TomahawkOS networking.  It defines the
 * fundamental data types shared across every layer of the stack and the
 * packet-buffer object (`netbuf_t`) that carries data up and down.
 *
 * ══════════════════════════════════════════════════════════════════════
 *  NETWORK STACK ARCHITECTURE  (layers from bottom to top)
 * ══════════════════════════════════════════════════════════════════════
 *
 *  ┌─────────────────────────────────────────────────────┐
 *  │  L5  Socket API (future — BSD-like send/recv/bind)  │
 *  ├─────────────────────────────────────────────────────┤
 *  │  L4  Transport    UDP  ·  TCP (future)              │
 *  ├─────────────────────────────────────────────────────┤
 *  │  L3  Network      IPv4  ·  ICMP                     │
 *  ├─────────────────────────────────────────────────────┤
 *  │  L2½ Resolution   ARP                               │
 *  ├─────────────────────────────────────────────────────┤
 *  │  L2  Data-link    Ethernet                          │
 *  ├─────────────────────────────────────────────────────┤
 *  │  L1  NIC driver   net_device abstraction            │
 *  │                   (e1000 / rtl8139 / virtio-net)    │
 *  └─────────────────────────────────────────────────────┘
 *
 * Packet flow:
 *
 *   TX path (send):
 *     App/Kernel → UDP/TCP → IPv4 → {ARP lookup} → Ethernet → NIC driver
 *
 *   RX path (receive):
 *     NIC driver → Ethernet demux → ARP / IPv4 → ICMP / UDP / TCP → App
 *
 * Every packet in flight is wrapped in a `netbuf_t` which carries a flat
 * byte buffer plus header-offset pointers so each layer can prepend or
 * strip its own header without copying.
 *
 * ══════════════════════════════════════════════════════════════════════
 */

#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 *  Fundamental network types
 * ==================================================================== */

/** 6-byte Ethernet MAC address (network byte order). */
typedef struct {
    uint8_t bytes[6];
} __attribute__((packed)) mac_addr_t;

/** 4-byte IPv4 address (network byte order). */
typedef struct {
    union {
        uint8_t  bytes[4];    /* e.g. {10, 0, 2, 15}               */
        uint32_t addr;        /* single 32-bit value (big-endian)   */
    };
} __attribute__((packed)) ipv4_addr_t;

/* ---- Handy constants ---- */

/** Broadcast MAC  ff:ff:ff:ff:ff:ff */
#define MAC_BROADCAST  ((mac_addr_t){{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}})

/** Zero / unset MAC  00:00:00:00:00:00 */
#define MAC_ZERO       ((mac_addr_t){{0,0,0,0,0,0}})

/** Broadcast IPv4  255.255.255.255 */
#define IPV4_BROADCAST ((ipv4_addr_t){.bytes = {255,255,255,255}})

/** Zero / unset IPv4  0.0.0.0 */
#define IPV4_ZERO      ((ipv4_addr_t){.bytes = {0,0,0,0}})

/* ---- Byte-order helpers (x86 is little-endian) ---- */

static inline uint16_t htons(uint16_t h) {
    return (uint16_t)((h >> 8) | (h << 8));
}
static inline uint16_t ntohs(uint16_t n) {
    return htons(n);   /* same operation on LE */
}
static inline uint32_t htonl(uint32_t h) {
    return ((h >> 24) & 0x000000FF)
         | ((h >>  8) & 0x0000FF00)
         | ((h <<  8) & 0x00FF0000)
         | ((h << 24) & 0xFF000000);
}
static inline uint32_t ntohl(uint32_t n) {
    return htonl(n);
}

/* ---- Address comparison helpers ---- */

static inline int mac_equal(mac_addr_t a, mac_addr_t b) {
    return a.bytes[0] == b.bytes[0] && a.bytes[1] == b.bytes[1]
        && a.bytes[2] == b.bytes[2] && a.bytes[3] == b.bytes[3]
        && a.bytes[4] == b.bytes[4] && a.bytes[5] == b.bytes[5];
}

static inline int ipv4_equal(ipv4_addr_t a, ipv4_addr_t b) {
    return a.addr == b.addr;
}

/* ---- Build addresses from octets ---- */

/** IPV4(10, 0, 2, 15) → ipv4_addr_t */
#define IPV4(a,b,c,d)  ((ipv4_addr_t){.bytes = {(a),(b),(c),(d)}})

/* ====================================================================
 *  netbuf_t — the packet buffer  (inspired by Linux sk_buff)
 * ==================================================================== */

/**
 *  Memory layout of a netbuf:
 *
 *      ┌─────────────────────────────────────────────────┐
 *      │  head-room  (space for headers to be prepended) │  ← buf
 *      ├─────────────────────────────────────────────────┤
 *      │  data →        payload region        ← tail     │
 *      ├─────────────────────────────────────────────────┤
 *      │  tail-room  (unused trailing space)             │  ← buf + capacity
 *      └─────────────────────────────────────────────────┘
 *
 *  `data` always points to the first valid byte.
 *  `len`  is the number of valid bytes starting at `data`.
 *
 *  To *prepend* a header, a layer calls  netbuf_push()  which moves
 *  `data` backwards.  To *strip* a header, netbuf_pull() moves `data`
 *  forwards.  To *append* payload, netbuf_put() extends `len`.
 */

#define NETBUF_MAX_SIZE  2048   /* max frame: 1518 Ethernet + head-room  */
#define NETBUF_HEADROOM    64   /* reserved space for protocol headers   */
#define NETBUF_POOL_SIZE   64   /* pre-allocated packet buffers          */

typedef struct netbuf {
    /* ---- buffer pointers ---- */
    uint8_t *buf;          /* start of the raw allocation               */
    uint8_t *data;         /* start of valid payload (moves on push/pull) */
    uint16_t len;          /* bytes of valid data starting at `data`    */
    uint16_t capacity;     /* total allocation (buf[0 .. capacity-1])   */

    /* ---- layer-specific offsets (set during RX demux) ---- */
    uint16_t mac_offset;   /* offset from `buf` to L2 Ethernet header  */
    uint16_t net_offset;   /* offset from `buf` to L3 IP header        */
    uint16_t transport_offset; /* offset from `buf` to L4 UDP/TCP hdr  */

    /* ---- metadata ---- */
    struct net_device *dev;   /* NIC this packet arrived on / will leave */

    /* ---- pool management ---- */
    int      in_use;       /* non-zero ⇒ buffer is currently allocated */
    struct netbuf *next;   /* free-list linkage                        */
} netbuf_t;

/* ---- netbuf allocation ---- */

/** Allocate a netbuf from the pool.  Returns NULL when exhausted. */
netbuf_t *netbuf_alloc(void);

/** Release a netbuf back to the pool. */
void netbuf_free(netbuf_t *nb);

/* ---- netbuf data manipulation ---- */

/**
 * @brief Reserve head-room (call before any push/put).
 *        Moves `data` forward by `len` bytes.
 */
void netbuf_reserve(netbuf_t *nb, uint16_t len);

/**
 * @brief Prepend `len` bytes of header space.
 *        Moves `data` backwards; returns pointer to the new space.
 */
uint8_t *netbuf_push(netbuf_t *nb, uint16_t len);

/**
 * @brief Append `len` bytes of payload space at the tail.
 *        Extends `len`; returns pointer to the new space.
 */
uint8_t *netbuf_put(netbuf_t *nb, uint16_t len);

/**
 * @brief Strip `len` bytes from the head (skip a received header).
 *        Moves `data` forward, decreases `len`.
 */
void netbuf_pull(netbuf_t *nb, uint16_t len);

/** Reset a netbuf to empty (preserving its raw allocation). */
void netbuf_reset(netbuf_t *nb);

/* ====================================================================
 *  Network subsystem lifecycle
 * ==================================================================== */

/**
 * @brief Initialise the entire network stack.
 *
 * Call once at boot.  Order of internal init:
 *   1. netbuf pool
 *   2. ARP table
 *   3. NIC probe & registration
 *   4. (future) socket layer
 */
void net_init(void);

#endif /* NET_H */
