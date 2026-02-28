/**
 * @file net.c
 * @brief Core Network Subsystem — netbuf pool, device registry, init
 *
 * This file implements the foundational pieces that every other network
 * module depends on:
 *
 *  1. **netbuf pool** — a fixed-size pool of pre-allocated packet
 *     buffers.  alloc/free are O(1) free-list operations.
 *
 *  2. **net_device registry** — tracks all registered NICs and
 *     provides lookup by name or index.
 *
 *  3. **net_init()** — boot-time entry point that initialises the
 *     netbuf pool, ARP cache, UDP bind table, and probes for NICs.
 */

#include "include/net.h"
#include "include/net_device.h"
#include "include/ethernet.h"
#include "include/arp.h"
#include "include/udp.h"
#include "include/loopback.h"
#include "include/socket.h"
#include "include/string.h"
#include <uart.h>

/* ====================================================================
 *  netbuf pool
 * ==================================================================== */

/** Raw backing store for every netbuf's byte buffer. */
static uint8_t  netbuf_raw[NETBUF_POOL_SIZE][NETBUF_MAX_SIZE];

/** The netbuf descriptors themselves. */
static netbuf_t netbuf_pool[NETBUF_POOL_SIZE];

/** Head of the free list (singly-linked via `next`). */
static netbuf_t *netbuf_free_list = NULL;

/** Initialise the netbuf pool (called by net_init). */
static void netbuf_pool_init(void)
{
    netbuf_free_list = NULL;
    for (int i = NETBUF_POOL_SIZE - 1; i >= 0; i--) {
        netbuf_t *nb    = &netbuf_pool[i];
        nb->buf         = netbuf_raw[i];
        nb->data        = nb->buf;
        nb->len         = 0;
        nb->capacity    = NETBUF_MAX_SIZE;
        nb->mac_offset  = 0;
        nb->net_offset  = 0;
        nb->transport_offset = 0;
        nb->dev         = NULL;
        nb->in_use      = 0;
        nb->next        = netbuf_free_list;
        netbuf_free_list = nb;
    }
}

netbuf_t *netbuf_alloc(void)
{
    if (!netbuf_free_list) return NULL;

    netbuf_t *nb = netbuf_free_list;
    netbuf_free_list = nb->next;

    nb->next    = NULL;
    nb->in_use  = 1;
    nb->data    = nb->buf;
    nb->len     = 0;
    nb->mac_offset       = 0;
    nb->net_offset       = 0;
    nb->transport_offset = 0;
    nb->dev     = NULL;

    return nb;
}

void netbuf_free(netbuf_t *nb)
{
    if (!nb) return;
    nb->in_use  = 0;
    nb->len     = 0;
    nb->data    = nb->buf;
    nb->dev     = NULL;
    nb->next    = netbuf_free_list;
    netbuf_free_list = nb;
}

/* ---- netbuf data manipulation ---- */

void netbuf_reserve(netbuf_t *nb, uint16_t headroom)
{
    nb->data += headroom;
}

uint8_t *netbuf_push(netbuf_t *nb, uint16_t len)
{
    nb->data -= len;
    nb->len  += len;
    return nb->data;
}

uint8_t *netbuf_put(netbuf_t *nb, uint16_t len)
{
    uint8_t *tail = nb->data + nb->len;
    nb->len += len;
    return tail;
}

void netbuf_pull(netbuf_t *nb, uint16_t len)
{
    if (len > nb->len) len = nb->len;
    nb->data += len;
    nb->len  -= len;
}

void netbuf_reset(netbuf_t *nb)
{
    nb->data  = nb->buf;
    nb->len   = 0;
    nb->mac_offset       = 0;
    nb->net_offset       = 0;
    nb->transport_offset = 0;
}

/* ====================================================================
 *  net_device registry
 * ==================================================================== */

static net_device_t *registered_devices[NET_MAX_DEVICES];
static int           device_count = 0;

int net_device_register(net_device_t *dev)
{
    if (device_count >= NET_MAX_DEVICES) return -1;
    dev->registered = 1;
    dev->tx_packets = 0;
    dev->tx_bytes   = 0;
    dev->rx_packets = 0;
    dev->rx_bytes   = 0;
    dev->tx_errors  = 0;
    dev->rx_errors  = 0;
    registered_devices[device_count++] = dev;

    uart_puts("[net] registered device: ");
    uart_puts(dev->name);
    uart_puts("\n");

    return 0;
}

net_device_t *net_device_get_by_name(const char *name)
{
    for (int i = 0; i < device_count; i++) {
        /* Simple strcmp */
        const char *a = registered_devices[i]->name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0')
            return registered_devices[i];
    }
    return NULL;
}

net_device_t *net_device_get_default(void)
{
    return device_count > 0 ? registered_devices[0] : NULL;
}

int net_device_count(void)
{
    return device_count;
}

net_device_t *net_device_get(int index)
{
    if (index < 0 || index >= device_count) return NULL;
    return registered_devices[index];
}

void net_device_set_ip(net_device_t *dev,
                       ipv4_addr_t ip,
                       ipv4_addr_t netmask,
                       ipv4_addr_t gateway)
{
    dev->ip      = ip;
    dev->netmask = netmask;
    dev->gateway = gateway;
}

/* ====================================================================
 *  Interface lifecycle
 * ==================================================================== */

int net_device_up(net_device_t *dev)
{
    if (!dev) return -1;
    if (dev->link_up) return 0;   /* already up — idempotent */

    if (dev->ops && dev->ops->start) {
        int ret = dev->ops->start(dev);
        if (ret != 0) {
            uart_puts("[net] net_device_up failed for ");
            uart_puts(dev->name);
            uart_puts(": err=");
            uart_putu((uint64_t)(uint32_t)-ret);
            uart_puts("\n");
            return ret;
        }
    }

    dev->link_up = 1;

    uart_puts("[net] ");
    uart_puts(dev->name);
    uart_puts(": interface UP\n");
    return 0;
}

int net_device_down(net_device_t *dev)
{
    if (!dev) return -1;
    if (!dev->link_up) return 0;  /* already down — idempotent */

    if (dev->ops && dev->ops->stop) {
        int ret = dev->ops->stop(dev);
        if (ret != 0) {
            uart_puts("[net] net_device_down failed for ");
            uart_puts(dev->name);
            uart_puts(": err=");
            uart_putu((uint64_t)(uint32_t)-ret);
            uart_puts("\n");
            return ret;
        }
    }

    dev->link_up = 0;

    uart_puts("[net] ");
    uart_puts(dev->name);
    uart_puts(": interface DOWN\n");
    return 0;
}

/* ====================================================================
 *  Raw transmit
 * ==================================================================== */

int net_device_transmit(net_device_t *dev, netbuf_t *nb)
{
    if (!dev || !nb) return -1;

    if (!dev->link_up) {
        dev->tx_errors++;
        return -1;
    }

    if (!dev->ops || !dev->ops->send) {
        dev->tx_errors++;
        return -1;
    }

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
 *  Receive pump
 * ==================================================================== */

void net_device_poll_all(void)
{
    netbuf_t *nb = netbuf_alloc();
    if (!nb) return;   /* pool exhausted — nothing we can do */

    for (int i = 0; i < device_count; i++) {
        net_device_t *dev = registered_devices[i];

        if (!dev->link_up || !dev->ops || !dev->ops->poll)
            continue;

        /*
         * Drain the device: poll() fills `nb` and returns 0 when a frame
         * is available, or -1 when the device queue is empty.
         * We loop until the queue is empty so burst arrivals are handled
         * in a single call rather than waiting for the next tick.
         */
        for (;;) {
            netbuf_reset(nb);
            nb->dev = dev;

            if (dev->ops->poll(dev, nb) != 0)
                break;   /* no more frames on this device */

            /*
             * Hand the frame to the Ethernet demux layer.
             * ethernet_receive() updates rx_packets / rx_bytes itself,
             * so we must NOT do it here to avoid double-counting.
             */
            ethernet_receive(dev, nb);
        }
    }

    netbuf_free(nb);
}

/* ====================================================================
 *  Routing helpers
 * ==================================================================== */

net_device_t *net_device_find_by_ip(ipv4_addr_t ip)
{
    for (int i = 0; i < device_count; i++) {
        if (ipv4_equal(registered_devices[i]->ip, ip))
            return registered_devices[i];
    }
    return NULL;
}

net_device_t *net_device_route(ipv4_addr_t dst_ip)
{
    net_device_t *fallback = NULL;

    for (int i = 0; i < device_count; i++) {
        net_device_t *dev = registered_devices[i];

        if (!dev->link_up) continue;

        /*
         * Check if dst_ip is within this device's subnet.
         * Uses a bitwise AND of both addresses with the device's netmask.
         */
        if ((dst_ip.addr & dev->netmask.addr) ==
            (dev->ip.addr  & dev->netmask.addr)) {
            return dev;   /* on-link: best possible match */
        }

        /* Track any device that has a configured default gateway */
        if (!ipv4_equal(dev->gateway, IPV4_ZERO))
            fallback = dev;
    }

    /* Off-link: use the device with a gateway (default route) */
    return fallback ? fallback : net_device_get_default();
}

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

/** Print a 4-byte IPv4 address as dotted-decimal to UART. */
static void print_ipv4(ipv4_addr_t ip)
{
    uart_putu(ip.bytes[0]); uart_puts(".");
    uart_putu(ip.bytes[1]); uart_puts(".");
    uart_putu(ip.bytes[2]); uart_puts(".");
    uart_putu(ip.bytes[3]);
}

/** Print a 6-byte MAC address as hex octets separated by ':' to UART. */
static void print_mac(mac_addr_t mac)
{
    for (int i = 0; i < 6; i++) {
        /* Print each byte as exactly 2 hex digits */
        uint8_t b = mac.bytes[i];
        uint8_t hi = b >> 4;
        uint8_t lo = b & 0x0F;
        char hex[3];
        hex[0] = (char)(hi < 10 ? '0' + hi : 'a' + hi - 10);
        hex[1] = (char)(lo < 10 ? '0' + lo : 'a' + lo - 10);
        hex[2] = '\0';
        uart_puts(hex);
        if (i < 5) uart_puts(":");
    }
}

void net_device_print_stats(net_device_t *dev)
{
    if (!dev) return;

    uart_puts("  [iface] ");
    uart_puts(dev->name);
    uart_puts("  link=");
    uart_puts(dev->link_up ? "UP" : "DOWN");

    uart_puts("  ip=");
    print_ipv4(dev->ip);

    uart_puts("/");
    /* Print netmask as CIDR prefix length */
    uint32_t nm = dev->netmask.addr;
    int prefix = 0;
    for (int b = 31; b >= 0; b--) {
        if (nm & (1u << b)) prefix++;
        else break;
    }
    uart_putu((uint64_t)prefix);

    uart_puts("  gw=");
    print_ipv4(dev->gateway);

    uart_puts("  mac=");
    print_mac(dev->mac);

    uart_puts("\n");

    uart_puts("         tx_pkt="); uart_putu(dev->tx_packets);
    uart_puts("  tx_bytes=");      uart_putu(dev->tx_bytes);
    uart_puts("  tx_err=");        uart_putu(dev->tx_errors);
    uart_puts("\n");

    uart_puts("         rx_pkt="); uart_putu(dev->rx_packets);
    uart_puts("  rx_bytes=");      uart_putu(dev->rx_bytes);
    uart_puts("  rx_err=");        uart_putu(dev->rx_errors);
    uart_puts("\n");
}

void net_device_print_all_stats(void)
{
    uart_puts("[net] network interface statistics:\n");
    if (device_count == 0) {
        uart_puts("  (no devices registered)\n");
        return;
    }
    for (int i = 0; i < device_count; i++) {
        net_device_print_stats(registered_devices[i]);
    }
}

/* ====================================================================
 *  Network stack initialisation
 * ==================================================================== */

void net_init(void)
{
    uart_puts("[net] Initialising network stack...\n");

    /* 1. Packet buffer pool */
    netbuf_pool_init();
    uart_puts("[net]   netbuf pool ready  (");
    uart_putu(NETBUF_POOL_SIZE);
    uart_puts(" buffers, ");
    uart_putu(NETBUF_MAX_SIZE);
    uart_puts(" B each)\n");

    /* 2. ARP cache */
    arp_init();
    uart_puts("[net]   ARP cache ready\n");

    /* 3. UDP bind table */
    udp_init();
    uart_puts("[net]   UDP layer ready\n");

    /* 4. Loopback interface (lo / 127.0.0.1) */
    loopback_init();

    /* 5. Socket layer */
    socket_init();

    /* 6. Print the interface table so it appears in the boot serial log */
    net_device_print_all_stats();

    uart_puts("[net] Network stack architecture initialised.\n");
}
