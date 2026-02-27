/**
 * @file ethernet.c
 * @brief Ethernet (L2) — frame TX/RX and demux
 */

#include "include/ethernet.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/arp.h"
#include "include/ipv4.h"
#include <uart.h>

/* ====================================================================
 *  Send
 * ==================================================================== */

int ethernet_send_frame(struct net_device *dev,
                        struct netbuf *nb,
                        mac_addr_t dst,
                        uint16_t ethertype)
{
    if (!dev || !dev->ops || !dev->ops->send) return -1;

    /* Prepend the 14-byte Ethernet header */
    eth_header_t *eth = (eth_header_t *)netbuf_push(nb, ETH_HEADER_LEN);
    eth->dst       = dst;
    eth->src       = dev->mac;
    eth->ethertype = htons(ethertype);

    /* Record the MAC offset */
    nb->mac_offset = (uint16_t)(nb->data - nb->buf);

    /* Hand to the NIC driver */
    int ret = dev->ops->send(dev, nb);
    if (ret == 0) {
        dev->tx_packets++;
        dev->tx_bytes += nb->len;
    } else {
        dev->tx_errors++;
    }
    return ret;
}

/* ====================================================================
 *  Receive & demux
 * ==================================================================== */

void ethernet_receive(struct net_device *dev, struct netbuf *nb)
{
    if (nb->len < ETH_HEADER_LEN) {
        dev->rx_errors++;
        return;
    }

    /* Parse the header (data currently points at byte 0 of the frame) */
    eth_header_t *eth = (eth_header_t *)nb->data;

    /* Check destination: unicast to us, or broadcast */
    if (!mac_equal(eth->dst, dev->mac)
     && !mac_equal(eth->dst, MAC_BROADCAST)) {
        return;   /* not for us */
    }

    uint16_t type = ntohs(eth->ethertype);

    /* Record offset & strip header */
    nb->mac_offset = (uint16_t)(nb->data - nb->buf);
    netbuf_pull(nb, ETH_HEADER_LEN);

    /* Update stats */
    dev->rx_packets++;
    dev->rx_bytes += nb->len;

    /* Dispatch to L3 / L2½ */
    switch (type) {
    case ETHERTYPE_ARP:
        arp_receive(dev, nb);
        break;

    case ETHERTYPE_IPV4:
        ipv4_receive(dev, nb);
        break;

    default:
        /* Unknown EtherType — drop silently */
        break;
    }
}
