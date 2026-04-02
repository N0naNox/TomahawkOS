/**
 * @file net_rx.c
 * @brief Network RX packet path — ISR-safe ring buffer and bottom-half processor
 *
 * See include/net_rx.h for the full architecture description.
 */

#include "include/net_rx.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/ethernet.h"
#include "include/spinlock.h"
#include <uart.h>

/* ====================================================================
 *  Internal ring entry
 * ==================================================================== */

typedef struct {
    net_device_t *dev;   /* NIC the frame arrived on   */
    netbuf_t     *nb;    /* netbuf holding the frame   */
} net_rx_entry_t;

/* ====================================================================
 *  Global ring state
 *
 *  The ring is a classic single-producer / single-consumer circular
 *  buffer, but here _both_ producer (ISR) and consumer (bottom half)
 *  can run on the same CPU with interrupts enabled between calls.
 *  We therefore protect every access with spin_lock_irqsave so that
 *  a timer ISR bottom-half call cannot be interrupted by another NIC
 *  ISR that also wants to enqueue.
 * ==================================================================== */

static net_rx_entry_t rx_ring[NET_RX_RING_SIZE];

/* head = next slot to write  (producer — ISR)        */
static volatile int rx_head  = 0;

/* tail = next slot to read   (consumer — bottom half) */
static volatile int rx_tail  = 0;

/* current occupancy */
static volatile int rx_count = 0;

static spinlock_t rx_lock = SPINLOCK_INIT;

/* ---- Lifetime counters ---- */
static volatile uint64_t stat_enqueued  = 0;
static volatile uint64_t stat_processed = 0;
static volatile uint64_t stat_dropped   = 0;

/* ====================================================================
 *  Lifecycle
 * ==================================================================== */

void net_rx_init(void)
{
    rx_head  = 0;
    rx_tail  = 0;
    rx_count = 0;

    stat_enqueued  = 0;
    stat_processed = 0;
    stat_dropped   = 0;

    for (int i = 0; i < NET_RX_RING_SIZE; i++) {
        rx_ring[i].dev = NULL;
        rx_ring[i].nb  = NULL;
    }

    uart_puts("[net_rx] RX ring ready (");
    uart_putu(NET_RX_RING_SIZE);
    uart_puts(" slots, ");
    uart_putu((uint64_t)(NET_RX_RING_SIZE * sizeof(net_rx_entry_t)));
    uart_puts(" B)\n");
}

/* ====================================================================
 *  Top-half: enqueue
 * ==================================================================== */

int net_rx_enqueue(net_device_t *dev, netbuf_t *nb)
{
    uint64_t flags;
    spin_lock_irqsave(&rx_lock, &flags);

    if (rx_count >= NET_RX_RING_SIZE) {
        /* Ring full — drop and count */
        if (dev) dev->rx_errors++;
        stat_dropped++;
        spin_unlock_irqrestore(&rx_lock, &flags);
        return -1;
    }

    rx_ring[rx_head].dev = dev;
    rx_ring[rx_head].nb  = nb;

    /* Bitmask wrap — works because NET_RX_RING_SIZE is a power of two */
    rx_head = (rx_head + 1) & (NET_RX_RING_SIZE - 1);
    rx_count++;
    stat_enqueued++;

    spin_unlock_irqrestore(&rx_lock, &flags);
    return 0;
}

/* ====================================================================
 *  Bottom-half: process
 * ==================================================================== */

int net_rx_process(void)
{
    int processed = 0;

    for (;;) {
        /*
         * Dequeue one entry while holding the lock.
         * We copy the entry out before releasing so the ring slot is
         * immediately available to the producer (ISR) again.
         */
        net_rx_entry_t entry;
        uint64_t flags;

        spin_lock_irqsave(&rx_lock, &flags);

        if (rx_count == 0) {
            spin_unlock_irqrestore(&rx_lock, &flags);
            break;
        }

        entry = rx_ring[rx_tail];
        rx_ring[rx_tail].dev = NULL;
        rx_ring[rx_tail].nb  = NULL;
        rx_tail = (rx_tail + 1) & (NET_RX_RING_SIZE - 1);
        rx_count--;

        spin_unlock_irqrestore(&rx_lock, &flags);

        /*
         * Process the frame outside the lock.
         * ethernet_receive() runs the full L2→L3→L4→socket demux
         * chain.  This may take many microseconds and we must not
         * hold rx_lock during that time or we'd starve the ISR.
         */
        if (entry.dev && entry.nb) {
            ethernet_receive(entry.dev, entry.nb);
            netbuf_free(entry.nb);
            stat_processed++;
        }

        processed++;
    }

    return processed;
}

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

int net_rx_pending(void)
{
    return rx_count;   /* single int read — atomic on x86-64 */
}

void net_rx_print_stats(void)
{
    uart_puts("[net_rx] ring stats: pending=");
    uart_putu((uint64_t)rx_count);
    uart_puts("  enqueued=");
    uart_putu(stat_enqueued);
    uart_puts("  processed=");
    uart_putu(stat_processed);
    uart_puts("  dropped=");
    uart_putu(stat_dropped);
    uart_puts("\n");
}
