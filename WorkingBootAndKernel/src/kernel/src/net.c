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
#include "include/arp.h"
#include "include/udp.h"
#include "include/loopback.h"
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

    uart_puts("[net] Network stack architecture initialised.\n");
}
