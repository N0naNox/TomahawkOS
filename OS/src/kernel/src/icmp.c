/**
 * @file icmp.c
 * @brief ICMP — Internet Control Message Protocol implementation
 */

#include "include/icmp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ipv4.h"
#include "include/checksum.h"
#include <uart.h>

/* ====================================================================
 *  Receive
 * ==================================================================== */

void icmp_receive(struct net_device *dev, struct netbuf *nb,
                  ipv4_addr_t src)
{
    if (nb->len < ICMP_HEADER_LEN) return;

    icmp_header_t *icmp = (icmp_header_t *)nb->data;

    /* Verify checksum over the entire ICMP message */
    if (net_checksum(nb->data, nb->len, 0) != 0) {
        uart_puts("[icmp] bad checksum, dropping\n");
        return;
    }

    switch (icmp->type) {

    case ICMP_TYPE_ECHO_REQUEST:
    {
        uart_puts("[icmp] echo request received, sending reply\n");

        /* Build Echo Reply — reuse the payload from the request */
        uint16_t payload_len = nb->len - ICMP_HEADER_LEN;

        netbuf_t *reply = netbuf_alloc();
        if (!reply) return;

        netbuf_reserve(reply, NETBUF_HEADROOM);

        /* Copy ICMP header + payload */
        uint8_t *out = netbuf_put(reply, nb->len);
        const uint8_t *in = nb->data;
        for (uint16_t i = 0; i < nb->len; i++) out[i] = in[i];

        /* Change type to Echo Reply, recompute checksum */
        icmp_header_t *rh = (icmp_header_t *)reply->data;
        rh->type     = ICMP_TYPE_ECHO_REPLY;
        rh->code     = 0;
        rh->checksum = 0;
        rh->checksum = net_checksum(reply->data, reply->len, 0);

        /* Send via IPv4 back to the requester */
        ipv4_send(dev, reply, src, IP_PROTO_ICMP);
        netbuf_free(reply);

        (void)payload_len;
        break;
    }

    case ICMP_TYPE_ECHO_REPLY:
        uart_puts("[icmp] echo reply received (id=");
        uart_putu(ntohs(icmp->identifier));
        uart_puts(" seq=");
        uart_putu(ntohs(icmp->sequence));
        uart_puts(")\n");
        break;

    default:
        uart_puts("[icmp] unhandled type=");
        uart_putu(icmp->type);
        uart_puts("\n");
        break;
    }
}

/* ====================================================================
 *  Send Echo Request (ping)
 * ==================================================================== */

int icmp_send_echo_request(struct net_device *dev,
                           ipv4_addr_t dst,
                           uint16_t id,
                           uint16_t seq,
                           const void *data,
                           uint16_t data_len)
{
    netbuf_t *nb = netbuf_alloc();
    if (!nb) return -1;

    netbuf_reserve(nb, NETBUF_HEADROOM);

    /* Append ICMP header */
    icmp_header_t *icmp = (icmp_header_t *)netbuf_put(nb, ICMP_HEADER_LEN);
    icmp->type       = ICMP_TYPE_ECHO_REQUEST;
    icmp->code       = 0;
    icmp->checksum   = 0;
    icmp->identifier = htons(id);
    icmp->sequence   = htons(seq);

    /* Append optional payload */
    if (data && data_len > 0) {
        uint8_t *p = netbuf_put(nb, data_len);
        const uint8_t *src = (const uint8_t *)data;
        for (uint16_t i = 0; i < data_len; i++) p[i] = src[i];
    }

    /* Compute ICMP checksum over header + payload */
    icmp->checksum = net_checksum(nb->data, nb->len, 0);

    int ret = ipv4_send(dev, nb, dst, IP_PROTO_ICMP);
    netbuf_free(nb);
    return ret;
}
