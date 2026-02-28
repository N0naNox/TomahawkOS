/**
 * @file ipv4.c
 * @brief IPv4 — Internet Protocol version 4 implementation
 */

#include "include/ipv4.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ethernet.h"
#include "include/arp.h"
#include "include/icmp.h"
#include "include/udp.h"
#include "include/checksum.h"
#include <uart.h>

/* Monotonically increasing identification for outgoing datagrams */
static uint16_t ip_id_counter = 0;

/* ====================================================================
 *  Send
 * ==================================================================== */

int ipv4_send(struct net_device *dev,
              struct netbuf *nb,
              ipv4_addr_t dst,
              uint8_t protocol)
{
    if (!dev) return -1;

    uint16_t payload_len = nb->len;

    /* Prepend 20-byte IPv4 header */
    ipv4_header_t *ip = (ipv4_header_t *)netbuf_push(nb, IPV4_HEADER_LEN);

    ip->version_ihl  = IPV4_MAKE_VER_IHL(4, 5);
    ip->dscp_ecn     = 0;
    ip->total_length  = htons(IPV4_HEADER_LEN + payload_len);
    ip->identification = htons(ip_id_counter++);
    ip->flags_fragoff  = htons(IPV4_FLAG_DF);   /* Don't Fragment */
    ip->ttl           = IPV4_DEFAULT_TTL;
    ip->protocol      = protocol;
    ip->checksum      = 0;        /* filled below */
    ip->src           = dev->ip;
    ip->dst           = dst;

    /* Compute header checksum */
    ip->checksum = net_checksum(ip, IPV4_HEADER_LEN, 0);

    /* Record net_offset */
    nb->net_offset = (uint16_t)(nb->data - nb->buf);

    /* ---- Broadcast / multicast short-circuit ----
     * 255.255.255.255 and the subnet broadcast go directly to MAC_BROADCAST
     * without ARP.  This is essential for DHCP Discover which must be sent
     * before we have any IP address or ARP cache entries.               */
    ipv4_addr_t subnet_bcast;
    subnet_bcast.addr = (dev->ip.addr | ~dev->netmask.addr);

    if (ipv4_equal(dst, IPV4_BROADCAST) || ipv4_equal(dst, subnet_bcast)) {
        return ethernet_send_frame(dev, nb, MAC_BROADCAST, ETHERTYPE_IPV4);
    }

    /* Determine next-hop: if dst is on-link, ARP for it directly;
       otherwise ARP for the gateway.
       Guard against unconfigured netmask (0.0.0.0): treat any destination
       as off-link when the netmask is zero so we use the gateway rather
       than trying to ARP for a remote address directly. */
    ipv4_addr_t next_hop = dst;
    int on_link = (dev->netmask.addr != 0)
               && ((dst.addr & dev->netmask.addr)
                   == (dev->ip.addr & dev->netmask.addr));
    if (!on_link && dev->gateway.addr != 0) {
        /* Off-link → go through gateway */
        next_hop = dev->gateway;
    }

    /* Resolve next-hop MAC via ARP */
    mac_addr_t dst_mac;
    if (arp_resolve(dev, next_hop, &dst_mac) != 0) {
        uart_puts("[ipv4] ARP pending for next-hop, packet dropped\n");
        return -1;   /* ARP not yet resolved */
    }

    return ethernet_send_frame(dev, nb, dst_mac, ETHERTYPE_IPV4);
}

/* ====================================================================
 *  Receive
 * ==================================================================== */

void ipv4_receive(struct net_device *dev, struct netbuf *nb)
{
    if (nb->len < IPV4_HEADER_LEN) return;

    ipv4_header_t *ip = (ipv4_header_t *)nb->data;

    /* Version check */
    if (IPV4_GET_VERSION(ip) != 4) return;

    /* Header length */
    int ihl = IPV4_GET_IHL(ip) * 4;
    if (ihl < IPV4_HEADER_LEN || ihl > nb->len) return;

    /* Verify header checksum */
    if (net_checksum(ip, (size_t)ihl, 0) != 0) {
        uart_puts("[ipv4] bad header checksum, dropping\n");
        dev->rx_errors++;
        return;
    }

    /* Is this packet addressed to us? (or broadcast) */
    if (!ipv4_equal(ip->dst, dev->ip)
     && !ipv4_equal(ip->dst, IPV4_BROADCAST)) {
        return;   /* not for us */
    }

    /* Save addresses before stripping header */
    ipv4_addr_t src = ip->src;
    ipv4_addr_t dst = ip->dst;
    uint8_t protocol = ip->protocol;
    uint16_t total_len = ntohs(ip->total_length);

    /* Record offset & strip IP header */
    nb->net_offset = (uint16_t)(nb->data - nb->buf);
    netbuf_pull(nb, (uint16_t)ihl);

    /* Clamp length to total_length (ignore trailing padding) */
    uint16_t payload_len = total_len - (uint16_t)ihl;
    if (nb->len > payload_len) nb->len = payload_len;

    (void)dst;  /* suppress unused warning */

    /* Dispatch to L4 */
    switch (protocol) {
    case IP_PROTO_ICMP:
        icmp_receive(dev, nb, src);
        break;

    case IP_PROTO_UDP:
        udp_receive(dev, nb, src, dst);
        break;

    case IP_PROTO_TCP:
        /* TODO: tcp_receive() */
        uart_puts("[ipv4] TCP packet received (not implemented)\n");
        break;

    default:
        break;
    }
}
