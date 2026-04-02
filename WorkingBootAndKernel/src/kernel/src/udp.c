/**
 * @file udp.c
 * @brief UDP — User Datagram Protocol implementation
 */

#include "include/udp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ipv4.h"
#include "include/checksum.h"
#include <uart.h>

/* ====================================================================
 *  Bind table
 * ==================================================================== */

typedef struct {
    uint16_t            port;
    udp_recv_callback_t cb;
    int                 active;
} udp_bind_entry_t;

static udp_bind_entry_t bind_table[UDP_MAX_BINDS];

void udp_init(void)
{
    for (int i = 0; i < UDP_MAX_BINDS; i++) {
        bind_table[i].active = 0;
    }
}

int udp_bind(uint16_t port, udp_recv_callback_t cb)
{
    /* Check for duplicate */
    for (int i = 0; i < UDP_MAX_BINDS; i++) {
        if (bind_table[i].active && bind_table[i].port == port)
            return -1;
    }
    /* Find free slot */
    for (int i = 0; i < UDP_MAX_BINDS; i++) {
        if (!bind_table[i].active) {
            bind_table[i].port   = port;
            bind_table[i].cb     = cb;
            bind_table[i].active = 1;
            return 0;
        }
    }
    return -1;   /* table full */
}

int udp_unbind(uint16_t port)
{
    for (int i = 0; i < UDP_MAX_BINDS; i++) {
        if (bind_table[i].active && bind_table[i].port == port) {
            bind_table[i].active = 0;
            return 0;
        }
    }
    return -1;
}

/* ====================================================================
 *  Send
 * ==================================================================== */

int udp_send(struct net_device *dev,
             ipv4_addr_t dst_ip,
             uint16_t src_port,
             uint16_t dst_port,
             const void *data,
             uint16_t data_len)
{
    netbuf_t *nb = netbuf_alloc();
    if (!nb) return -1;

    netbuf_reserve(nb, NETBUF_HEADROOM);

    /* Append payload first */
    if (data && data_len > 0) {
        uint8_t *p = netbuf_put(nb, data_len);
        const uint8_t *src = (const uint8_t *)data;
        for (uint16_t i = 0; i < data_len; i++) p[i] = src[i];
    }

    /* Prepend UDP header */
    udp_header_t *udp = (udp_header_t *)netbuf_push(nb, UDP_HEADER_LEN);
    uint16_t udp_len = UDP_HEADER_LEN + data_len;

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(udp_len);
    udp->checksum = 0;   /* optional in IPv4 UDP — set to 0 for now */

    /* Compute UDP checksum with pseudo-header */
    udp_pseudo_header_t pseudo;
    pseudo.src        = dev->ip;
    pseudo.dst        = dst_ip;
    pseudo.zero       = 0;
    pseudo.protocol   = IP_PROTO_UDP;
    pseudo.udp_length = htons(udp_len);

    /* Sum pseudo-header, then continue with UDP header+payload */
    uint32_t csum = 0;
    /* Accumulate pseudo-header */
    {
        const uint8_t *p = (const uint8_t *)&pseudo;
        for (int i = 0; i < (int)sizeof(pseudo); i += 2) {
            csum += (uint16_t)(p[i] | (p[i+1] << 8));
        }
    }
    /* Accumulate UDP header + payload (use net_checksum with initial) */
    udp->checksum = net_checksum(nb->data, nb->len, csum);
    if (udp->checksum == 0) udp->checksum = 0xFFFF;  /* 0 means "no checksum" */

    /* Record transport offset */
    nb->transport_offset = (uint16_t)(nb->data - nb->buf);

    int ret = ipv4_send(dev, nb, dst_ip, IP_PROTO_UDP);
    netbuf_free(nb);
    return ret;
}

/* ====================================================================
 *  Receive
 * ==================================================================== */

void udp_receive(struct net_device *dev, struct netbuf *nb,
                 ipv4_addr_t src, ipv4_addr_t dst)
{
    if (nb->len < UDP_HEADER_LEN) return;

    udp_header_t *udp = (udp_header_t *)nb->data;
    uint16_t sport = ntohs(udp->src_port);
    uint16_t dport = ntohs(udp->dst_port);
    uint16_t ulen  = ntohs(udp->length);

    (void)dst;

    if (ulen < UDP_HEADER_LEN || ulen > nb->len) return;

    /* Point past the UDP header to the payload */
    const uint8_t *payload = nb->data + UDP_HEADER_LEN;
    uint16_t payload_len   = ulen - UDP_HEADER_LEN;

    /* Look up registered handler for this port */
    for (int i = 0; i < UDP_MAX_BINDS; i++) {
        if (bind_table[i].active && bind_table[i].port == dport) {
            bind_table[i].cb(dev, src, sport, payload, payload_len);
            return;
        }
    }

    /* No handler — silently drop (or send ICMP port unreachable later) */
}
