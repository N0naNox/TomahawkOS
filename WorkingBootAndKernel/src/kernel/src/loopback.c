/**
 * @file loopback.c
 * @brief Loopback Network Interface — implementation
 *
 * The loopback driver is the simplest possible net_device:
 *
 *   send():  copy the transmitted frame into a fresh netbuf, then
 *            call ethernet_receive() to feed it back into the RX path
 *            of the same device.  This exercises every layer of the
 *            stack (Ethernet demux → IPv4 → ICMP / UDP) with zero
 *            hardware dependencies.
 *
 *   poll():  always returns -1 (nothing to poll — all RX happens
 *            synchronously inside send).
 */

#include "include/loopback.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ethernet.h"
#include "include/arp.h"
#include <uart.h>

/* ====================================================================
 *  Loopback driver state
 * ==================================================================== */

/** The single loopback device instance. */
static net_device_t lo_device;

/** Driver ops (forward declarations). */
static int lo_send(struct net_device *dev, struct netbuf *nb);
static int lo_poll(struct net_device *dev, struct netbuf *nb);
static int lo_start(struct net_device *dev);
static int lo_stop(struct net_device *dev);

static net_device_ops_t lo_ops = {
    .send  = lo_send,
    .poll  = lo_poll,
    .start = lo_start,
    .stop  = lo_stop,
};

/* ====================================================================
 *  Driver callbacks
 * ==================================================================== */

/**
 * @brief Loopback send — copy frame and inject it as a received frame.
 *
 * `nb->data` points to a fully formed Ethernet frame (header already
 * prepended by ethernet_send_frame).  We allocate a new netbuf,
 * copy the frame data, and call ethernet_receive() so the RX path
 * processes it exactly as if it arrived from a real NIC.
 */
static int lo_send(struct net_device *dev, struct netbuf *nb)
{
    if (!nb || nb->len == 0) return -1;

    /* Allocate a fresh buffer for the "received" copy */
    netbuf_t *rx = netbuf_alloc();
    if (!rx) {
        uart_puts("[lo] send: netbuf_alloc failed (pool exhausted)\n");
        return -1;
    }

    /* Copy the transmitted frame into the RX buffer */
    uint8_t *dst = netbuf_put(rx, nb->len);
    const uint8_t *src = nb->data;
    for (uint16_t i = 0; i < nb->len; i++) {
        dst[i] = src[i];
    }
    rx->dev = dev;

    /* Feed the copy into the receive path */
    ethernet_receive(dev, rx);

    /* Free the RX buffer (layers don't hold onto it) */
    netbuf_free(rx);

    return 0;
}

/** Loopback poll — nothing to poll, RX is synchronous via send(). */
static int lo_poll(struct net_device *dev, struct netbuf *nb)
{
    (void)dev;
    (void)nb;
    return -1;   /* no frame available */
}

static int lo_start(struct net_device *dev)
{
    dev->link_up = 1;
    uart_puts("[lo] link up\n");
    return 0;
}

static int lo_stop(struct net_device *dev)
{
    dev->link_up = 0;
    uart_puts("[lo] link down\n");
    return 0;
}

/* ====================================================================
 *  Initialisation
 * ==================================================================== */

void loopback_init(void)
{
    net_device_t *lo = &lo_device;

    /* Identity */
    lo->name[0] = 'l';
    lo->name[1] = 'o';
    lo->name[2] = '\0';

    lo->mac = MAC_ZERO;   /* loopback has no real MAC */

    /* Driver vtable */
    lo->ops  = &lo_ops;
    lo->priv = NULL;

    /* L3 configuration: 127.0.0.1 / 255.0.0.0 */
    lo->ip      = IPV4(127, 0, 0, 1);
    lo->netmask = IPV4(255, 0, 0, 0);
    lo->gateway = IPV4_ZERO;

    /* Link is always up */
    lo->link_up = 1;

    /* Register with the device table */
    net_device_register(lo);

    /* Pre-populate ARP cache so 127.0.0.1 resolves instantly
     * (no broadcast needed on loopback). */
    arp_cache_update(IPV4(127, 0, 0, 1), MAC_ZERO);

    uart_puts("[lo] loopback interface ready  (127.0.0.1/8)\n");
}

net_device_t *loopback_dev(void)
{
    return lo_device.registered ? &lo_device : NULL;
}
