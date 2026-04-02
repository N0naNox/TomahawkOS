/**
 * @file arp.c
 * @brief ARP — Address Resolution Protocol implementation
 *
 * Maintains the ARP cache and handles request/reply exchange.
 */

#include "include/arp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ethernet.h"
#include "include/checksum.h"
#include "include/string.h"
#include <uart.h>

/* ====================================================================
 *  ARP cache
 * ==================================================================== */

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

void arp_init(void)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].state = ARP_STATE_FREE;
    }
}

/* ---- cache helpers ---- */

int arp_cache_lookup(ipv4_addr_t ip, mac_addr_t *out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_RESOLVED
         && ipv4_equal(arp_cache[i].ip, ip)) {
            *out = arp_cache[i].mac;
            return 0;
        }
    }
    return -1;
}

void arp_cache_update(ipv4_addr_t ip, mac_addr_t mac)
{
    /* Update existing entry if present */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE
         && ipv4_equal(arp_cache[i].ip, ip)) {
            arp_cache[i].mac   = mac;
            arp_cache[i].state = ARP_STATE_RESOLVED;
            return;
        }
    }
    /* Insert into first free slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) {
            arp_cache[i].ip    = ip;
            arp_cache[i].mac   = mac;
            arp_cache[i].state = ARP_STATE_RESOLVED;
            return;
        }
    }
    /* Cache full — overwrite slot 0 (simple eviction) */
    arp_cache[0].ip    = ip;
    arp_cache[0].mac   = mac;
    arp_cache[0].state = ARP_STATE_RESOLVED;
}

/* ====================================================================
 *  ARP send
 * ==================================================================== */

int arp_send_request(struct net_device *dev, ipv4_addr_t target_ip)
{
    netbuf_t *nb = netbuf_alloc();
    if (!nb) return -1;

    netbuf_reserve(nb, NETBUF_HEADROOM);

    /* Build ARP payload */
    arp_header_t *arp = (arp_header_t *)netbuf_put(nb, sizeof(arp_header_t));
    arp->htype      = htons(ARP_HTYPE_ETHERNET);
    arp->ptype      = htons(ARP_PTYPE_IPV4);
    arp->hlen       = ARP_HLEN_ETH;
    arp->plen       = ARP_PLEN_IPV4;
    arp->oper       = htons(ARP_OP_REQUEST);
    arp->sender_mac = dev->mac;
    arp->sender_ip  = dev->ip;
    arp->target_mac = MAC_ZERO;     /* unknown — that's what we're asking */
    arp->target_ip  = target_ip;

    /* Mark as pending in cache */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) {
            arp_cache[i].ip    = target_ip;
            arp_cache[i].mac   = MAC_ZERO;
            arp_cache[i].state = ARP_STATE_PENDING;
            break;
        }
    }

    int ret = ethernet_send_frame(dev, nb, MAC_BROADCAST, ETHERTYPE_ARP);
    netbuf_free(nb);
    return ret;
}

int arp_resolve(struct net_device *dev, ipv4_addr_t ip, mac_addr_t *out)
{
    if (arp_cache_lookup(ip, out) == 0)
        return 0;   /* already resolved */

    arp_send_request(dev, ip);
    return -1;      /* pending — caller should retry */
}

/* ====================================================================
 *  ARP receive
 * ==================================================================== */

void arp_receive(struct net_device *dev, struct netbuf *nb)
{
    if (nb->len < sizeof(arp_header_t)) return;

    arp_header_t *arp = (arp_header_t *)nb->data;

    /* Only handle Ethernet/IPv4 */
    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET) return;
    if (ntohs(arp->ptype) != ARP_PTYPE_IPV4)     return;

    /* Always update cache with sender's info (RFC 826) */
    arp_cache_update(arp->sender_ip, arp->sender_mac);

    uint16_t oper = ntohs(arp->oper);

    if (oper == ARP_OP_REQUEST) {
        /* Is this request for our IP? */
        if (!ipv4_equal(arp->target_ip, dev->ip)) return;

        uart_puts("[arp] request for our IP, sending reply\n");

        /* Build ARP reply */
        netbuf_t *reply = netbuf_alloc();
        if (!reply) return;

        netbuf_reserve(reply, NETBUF_HEADROOM);
        arp_header_t *r = (arp_header_t *)netbuf_put(reply, sizeof(arp_header_t));
        r->htype      = htons(ARP_HTYPE_ETHERNET);
        r->ptype      = htons(ARP_PTYPE_IPV4);
        r->hlen       = ARP_HLEN_ETH;
        r->plen       = ARP_PLEN_IPV4;
        r->oper       = htons(ARP_OP_REPLY);
        r->sender_mac = dev->mac;
        r->sender_ip  = dev->ip;
        r->target_mac = arp->sender_mac;
        r->target_ip  = arp->sender_ip;

        ethernet_send_frame(dev, reply, arp->sender_mac, ETHERTYPE_ARP);
        netbuf_free(reply);
    }
    else if (oper == ARP_OP_REPLY) {
        uart_puts("[arp] reply received, cache updated\n");
        /* Cache was already updated above */
    }
}

/* ====================================================================
 *  Debug
 * ==================================================================== */

void arp_print_cache(void)
{
    uart_puts("[arp] cache:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) continue;
        uart_puts("  ");
        /* Print IP octets */
        for (int j = 0; j < 4; j++) {
            uart_putu(arp_cache[i].ip.bytes[j]);
            if (j < 3) uart_putchar('.');
        }
        uart_puts(" -> ");
        /* Print MAC bytes */
        for (int j = 0; j < 6; j++) {
            uart_puthex(arp_cache[i].mac.bytes[j]);
            if (j < 5) uart_putchar(':');
        }
        uart_puts(arp_cache[i].state == ARP_STATE_PENDING
                  ? " (pending)\n" : " (resolved)\n");
    }
}
