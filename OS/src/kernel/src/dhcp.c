/**
 * @file dhcp.c
 * @brief Minimal DHCP client — Discover → Offer → Request → Ack
 *
 * ═══════════════════════════════════════════════════════════════════
 *  PROTOCOL OVERVIEW
 * ═══════════════════════════════════════════════════════════════════
 *
 *  1. Client broadcasts DHCP Discover  (UDP 0.0.0.0:68 → 255.255.255.255:67)
 *  2. Server unicasts/broadcasts DHCP Offer  (src port 67)
 *  3. Client broadcasts DHCP Request   (same XID, option 50 = offered IP,
 *                                       option 54 = server IP)
 *  4. Server broadcasts DHCP Ack       (confirms the lease)
 *
 *  The implementation uses the synchronous UDP callback mechanism:
 *   • Register a callback on port 68 before sending.
 *   • Spin calling net_device_poll_all() + net_rx_process() to pump
 *     frames through the stack until the callback is triggered.
 *   • Flags (dhcp_got_offer / dhcp_got_ack) indicate callback fired.
 *
 *  QEMU user-mode networking (−netdev user, −device e1000):
 *   DHCP server address : 10.0.2.2
 *   Assigned client IP  : 10.0.2.15
 *   Subnet mask         : 255.255.255.0
 *   Default gateway     : 10.0.2.2
 * ═══════════════════════════════════════════════════════════════════
 */

#include "include/dhcp.h"
#include "include/udp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/net_rx.h"
#include <uart.h>
#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 *  BOOTP / DHCP constants
 * ==================================================================== */

#define DHCP_XID          0xDEADBEEFu  /* arbitrary transaction ID */

/* DHCP magic cookie in network byte order */
#define DHCP_MAGIC_0  99u
#define DHCP_MAGIC_1  130u
#define DHCP_MAGIC_2  83u
#define DHCP_MAGIC_3  99u

/* DHCP option codes */
#define DHCP_OPT_MSG_TYPE     53u
#define DHCP_OPT_SERVER_ID    54u
#define DHCP_OPT_REQUESTED_IP 50u
#define DHCP_OPT_SUBNET_MASK  1u
#define DHCP_OPT_ROUTER       3u
#define DHCP_OPT_END          255u

/* DHCP message types (option 53 values) */
#define DHCP_MSG_DISCOVER  1u
#define DHCP_MSG_OFFER     2u
#define DHCP_MSG_REQUEST   3u
#define DHCP_MSG_ACK       5u

/* BOOTP operation codes */
#define BOOTREQUEST  1u

/* Broadcast flags field */
#define DHCP_FLAG_BROADCAST  0x0080u   /* network byte-order bit 15 */

/* ====================================================================
 *  BOOTP fixed header  (236 bytes before options)
 * ==================================================================== */

typedef struct __attribute__((packed)) {
    uint8_t  op;          /* BOOTREQUEST = 1                     */
    uint8_t  htype;       /* hardware type: 1 = Ethernet         */
    uint8_t  hlen;        /* hardware addr len: 6 for MAC        */
    uint8_t  hops;        /* relay agent hops (0)                */
    uint32_t xid;         /* transaction ID                      */
    uint16_t secs;        /* seconds since DHCP started (0)      */
    uint16_t flags;       /* 0x8000 = broadcast                  */
    uint8_t  ciaddr[4];   /* client IP (0 before bound)          */
    uint8_t  yiaddr[4];   /* your (offered) IP                   */
    uint8_t  siaddr[4];   /* server IP                           */
    uint8_t  giaddr[4];   /* relay agent IP (0)                  */
    uint8_t  chaddr[16];  /* client MAC + 10 bytes padding       */
    uint8_t  sname[64];   /* server name (zeroed)                */
    uint8_t  file[128];   /* boot file (zeroed)                  */
    /* DHCP options follow (magic cookie first) */
} dhcp_bootp_t;

/* Total constant size: 236 bytes */
#define DHCP_BOOTP_SIZE  sizeof(dhcp_bootp_t)  /* = 236 */

/* Packet buffer: BOOTP header + options (max 64 bytes of options) */
#define DHCP_PKT_SIZE  (DHCP_BOOTP_SIZE + 64u)

/* ====================================================================
 *  Module-level state (global so callback can write it)
 * ==================================================================== */

static volatile int  dhcp_got_offer = 0;
static volatile int  dhcp_got_ack   = 0;

/* IPs captured from OFFER / ACK */
static uint8_t dhcp_offered_ip[4] = {0};
static uint8_t dhcp_server_ip[4]  = {0};
static uint8_t dhcp_mask[4]       = {255, 255, 255, 0};
static uint8_t dhcp_gw[4]         = {0};

/* The device we are configuring (set by dhcp_discover) */
static net_device_t *dhcp_dev = NULL;

/* ====================================================================
 *  Helper: big-endian 32-bit write / read
 * ==================================================================== */

static inline void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v);
}

/* ====================================================================
 *  Packet builder
 * ==================================================================== */

/**
 * @brief Fill @p pkt with a DHCP Discover or Request datagram.
 *
 * @param pkt       Output buffer (at least DHCP_PKT_SIZE bytes)
 * @param mac       Client MAC address
 * @param msg_type  DHCP_MSG_DISCOVER or DHCP_MSG_REQUEST
 * @param offered   Non-NULL for Request: the offered IP to request
 * @param server    Non-NULL for Request: the server IP (option 54)
 * @return  total number of bytes written into pkt
 */
static uint16_t dhcp_build(uint8_t *pkt, const mac_addr_t *mac,
                            uint8_t msg_type,
                            const uint8_t *offered,
                            const uint8_t *server)
{
    /* Zero the entire buffer first */
    for (uint16_t i = 0; i < DHCP_PKT_SIZE; i++) pkt[i] = 0;

    dhcp_bootp_t *b = (dhcp_bootp_t *)pkt;

    b->op    = BOOTREQUEST;
    b->htype = 1;
    b->hlen  = 6;
    b->hops  = 0;

    /* Transaction ID: big-endian */
    put_be32((uint8_t *)&b->xid, DHCP_XID);

    b->secs  = 0;
    /* Broadcast flag (big-endian bit 15) */
    b->flags = 0x0080u;   /* already in BE on a LE host this sets bit 7 of byte 0 */

    /* Copy MAC into chaddr */
    for (int i = 0; i < 6; i++) b->chaddr[i] = mac->bytes[i];

    /* Options start right after the BOOTP fixed header */
    uint8_t *opt = pkt + DHCP_BOOTP_SIZE;
    uint16_t off = 0;

    /* Magic cookie */
    opt[off++] = DHCP_MAGIC_0;
    opt[off++] = DHCP_MAGIC_1;
    opt[off++] = DHCP_MAGIC_2;
    opt[off++] = DHCP_MAGIC_3;

    /* Option 53: DHCP message type */
    opt[off++] = DHCP_OPT_MSG_TYPE;
    opt[off++] = 1;              /* length */
    opt[off++] = msg_type;

    if (msg_type == DHCP_MSG_REQUEST && offered && server) {
        /* Option 50: Requested IP Address */
        opt[off++] = DHCP_OPT_REQUESTED_IP;
        opt[off++] = 4;
        opt[off++] = offered[0]; opt[off++] = offered[1];
        opt[off++] = offered[2]; opt[off++] = offered[3];

        /* Option 54: DHCP Server Identifier */
        opt[off++] = DHCP_OPT_SERVER_ID;
        opt[off++] = 4;
        opt[off++] = server[0]; opt[off++] = server[1];
        opt[off++] = server[2]; opt[off++] = server[3];
    }

    /* End option */
    opt[off++] = DHCP_OPT_END;

    return (uint16_t)(DHCP_BOOTP_SIZE + off);
}

/* ====================================================================
 *  DHCP options parser helpers
 * ==================================================================== */

/**
 * @brief Walk DHCP options (past the magic cookie) looking for @p target.
 * @return Pointer to the first data byte of the option, or NULL.
 */
static const uint8_t *dhcp_find_option(const uint8_t *opts, uint16_t opts_len,
                                        uint8_t target)
{
    uint16_t i = 0;
    while (i < opts_len) {
        uint8_t code = opts[i++];
        if (code == DHCP_OPT_END) break;
        if (code == 0) continue;           /* pad byte */
        if (i >= opts_len) break;
        uint8_t len = opts[i++];
        if (code == target) return &opts[i];
        i += len;
    }
    return NULL;
}

/* ====================================================================
 *  UDP callback (handles both Offer and Ack)
 * ==================================================================== */

static void dhcp_rx_callback(struct net_device *dev,
                              ipv4_addr_t src_ip,
                              uint16_t src_port,
                              const uint8_t *data,
                              uint16_t data_len)
{
    (void)dev;
    (void)src_ip;

    /* Only care about traffic from the DHCP server port */
    if (src_port != 67u) return;

    /* Minimum size: BOOTP header + magic cookie (4) + end option (1) */
    if (data_len < (uint16_t)(DHCP_BOOTP_SIZE + 5)) return;

    const dhcp_bootp_t *b = (const dhcp_bootp_t *)data;

    /* Verify transaction ID */
    uint32_t xid_be;
    const uint8_t *xb = (const uint8_t *)&b->xid;
    xid_be = ((uint32_t)xb[0] << 24) | ((uint32_t)xb[1] << 16)
           | ((uint32_t)xb[2] <<  8) | (uint32_t)xb[3];
    if (xid_be != DHCP_XID) return;

    /* Point at options (past BOOTP header) */
    const uint8_t *opts    = data + DHCP_BOOTP_SIZE;
    uint16_t       opts_len = (uint16_t)(data_len - DHCP_BOOTP_SIZE);

    /* Verify magic cookie */
    if (opts_len < 4) return;
    if (opts[0] != DHCP_MAGIC_0 || opts[1] != DHCP_MAGIC_1 ||
        opts[2] != DHCP_MAGIC_2 || opts[3] != DHCP_MAGIC_3) return;
    opts     += 4;
    opts_len -= 4;

    /* Read DHCP message type (option 53) */
    const uint8_t *msg_type_ptr = dhcp_find_option(opts, opts_len, DHCP_OPT_MSG_TYPE);
    if (!msg_type_ptr) return;
    uint8_t msg_type = *msg_type_ptr;

    if (msg_type == DHCP_MSG_OFFER && !dhcp_got_offer) {
        /* Capture offered IP from yiaddr */
        dhcp_offered_ip[0] = b->yiaddr[0];
        dhcp_offered_ip[1] = b->yiaddr[1];
        dhcp_offered_ip[2] = b->yiaddr[2];
        dhcp_offered_ip[3] = b->yiaddr[3];

        /* Capture server IP from option 54 (or siaddr fallback) */
        const uint8_t *srv = dhcp_find_option(opts, opts_len, DHCP_OPT_SERVER_ID);
        if (srv) {
            dhcp_server_ip[0] = srv[0]; dhcp_server_ip[1] = srv[1];
            dhcp_server_ip[2] = srv[2]; dhcp_server_ip[3] = srv[3];
        } else {
            dhcp_server_ip[0] = b->siaddr[0]; dhcp_server_ip[1] = b->siaddr[1];
            dhcp_server_ip[2] = b->siaddr[2]; dhcp_server_ip[3] = b->siaddr[3];
        }

        uart_puts("[dhcp] Offer received: ");
        for (int i = 0; i < 4; i++) {
            uart_putu(dhcp_offered_ip[i]);
            if (i < 3) uart_puts(".");
        }
        uart_puts("\n");

        dhcp_got_offer = 1;
    }
    else if (msg_type == DHCP_MSG_ACK && !dhcp_got_ack) {
        /* Extract subnet mask */
        const uint8_t *mask = dhcp_find_option(opts, opts_len, DHCP_OPT_SUBNET_MASK);
        if (mask) {
            dhcp_mask[0] = mask[0]; dhcp_mask[1] = mask[1];
            dhcp_mask[2] = mask[2]; dhcp_mask[3] = mask[3];
        }

        /* Extract default gateway */
        const uint8_t *gw = dhcp_find_option(opts, opts_len, DHCP_OPT_ROUTER);
        if (gw) {
            dhcp_gw[0] = gw[0]; dhcp_gw[1] = gw[1];
            dhcp_gw[2] = gw[2]; dhcp_gw[3] = gw[3];
        } else {
            /* Fall back to server IP as gateway */
            dhcp_gw[0] = dhcp_server_ip[0]; dhcp_gw[1] = dhcp_server_ip[1];
            dhcp_gw[2] = dhcp_server_ip[2]; dhcp_gw[3] = dhcp_server_ip[3];
        }

        uart_puts("[dhcp] Ack received — lease obtained\n");

        /* Configure the device */
        if (dhcp_dev) {
            ipv4_addr_t ip  = {{dhcp_offered_ip[0], dhcp_offered_ip[1],
                                 dhcp_offered_ip[2], dhcp_offered_ip[3]}};
            ipv4_addr_t nm  = {{dhcp_mask[0], dhcp_mask[1],
                                 dhcp_mask[2], dhcp_mask[3]}};
            ipv4_addr_t gwa = {{dhcp_gw[0], dhcp_gw[1],
                                 dhcp_gw[2], dhcp_gw[3]}};
            net_device_set_ip(dhcp_dev, ip, nm, gwa);
        }

        dhcp_got_ack = 1;
    }
}

/* ====================================================================
 *  Public API
 * ==================================================================== */

/**
 * @brief Synchronous DHCP four-way handshake.
 *
 * Sends Discover, waits for Offer, sends Request, waits for Ack.
 * Configures dev->ip / netmask / gateway on success.
 */
int dhcp_discover(net_device_t *dev)
{
    if (!dev || !dev->link_up) return -1;

    /* Reset state */
    dhcp_dev       = dev;
    dhcp_got_offer = 0;
    dhcp_got_ack   = 0;
    for (int i = 0; i < 4; i++) {
        dhcp_offered_ip[i] = 0;
        dhcp_server_ip[i]  = 0;
        dhcp_mask[i]       = (i < 3) ? 255 : 0;
        dhcp_gw[i]         = 0;
    }

    /* Bind port 68 to our callback */
    udp_bind(68, dhcp_rx_callback);

    /* ----------------------------------------------------------------
     *  Phase 1: Send Discover and wait for Offer
     * ---------------------------------------------------------------- */

    static uint8_t pkt[DHCP_PKT_SIZE];
    uint16_t pkt_len = dhcp_build(pkt, &dev->mac,
                                   DHCP_MSG_DISCOVER, NULL, NULL);

    uart_puts("[dhcp] Sending DHCP Discover...\n");

    /* Source IP is 0.0.0.0 until we have a lease (dev->ip should be zero) */
    udp_send(dev, IPV4_BROADCAST, 68, 67, pkt, pkt_len);

    /* Poll for Offer (up to ~1 second: 1 000 000 iterations with a little delay) */
    int offer_timeout = 2000000;
    while (!dhcp_got_offer && offer_timeout-- > 0) {
        net_device_poll_all();
        net_rx_process();
        /* tiny delay */
        for (volatile int d = 0; d < 100; d++) {}
    }

    if (!dhcp_got_offer) {
        uart_puts("[dhcp] Offer timeout — DHCP failed\n");
        udp_unbind(68);
        return -1;
    }

    /* ----------------------------------------------------------------
     *  Phase 2: Send Request and wait for Ack
     * ---------------------------------------------------------------- */

    pkt_len = dhcp_build(pkt, &dev->mac,
                          DHCP_MSG_REQUEST,
                          dhcp_offered_ip, dhcp_server_ip);

    uart_puts("[dhcp] Sending DHCP Request...\n");
    udp_send(dev, IPV4_BROADCAST, 68, 67, pkt, pkt_len);

    int ack_timeout = 2000000;
    while (!dhcp_got_ack && ack_timeout-- > 0) {
        net_device_poll_all();
        net_rx_process();
        for (volatile int d = 0; d < 100; d++) {}
    }

    udp_unbind(68);

    if (!dhcp_got_ack) {
        uart_puts("[dhcp] Ack timeout — DHCP failed\n");
        return -1;
    }

    /* net_device_set_ip already called inside the callback */
    uart_puts("[dhcp] Configuration complete: ");
    for (int i = 0; i < 4; i++) {
        uart_putu(dev->ip.bytes[i]);
        if (i < 3) uart_puts(".");
    }
    uart_puts("/");
    for (int i = 0; i < 4; i++) {
        uart_putu(dev->netmask.bytes[i]);
        if (i < 3) uart_puts(".");
    }
    uart_puts(" gw ");
    for (int i = 0; i < 4; i++) {
        uart_putu(dev->gateway.bytes[i]);
        if (i < 3) uart_puts(".");
    }
    uart_puts("\n");

    return 0;
}
