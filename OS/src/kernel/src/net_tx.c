/**
 * @file net_tx.c
 * @brief Network TX packet path — deferred-transmit ring and flush
 *
 * See include/net_tx.h for the full architecture description.
 */

#include "include/net_tx.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/spinlock.h"
#include <uart.h>

/* ====================================================================
 *  Internal ring entry
 * ==================================================================== */

typedef struct {
    net_device_t *dev;   /* NIC to transmit on   */
    netbuf_t     *nb;    /* netbuf with the frame */
} net_tx_entry_t;

/* ====================================================================
 *  Global ring state
 *
 *  Single-producer / single-consumer FIFO, protected by spinlock so
 *  both producer (any context, including ISR) and consumer (timer tick)
 *  can run safely with preemption and interrupts in play.
 * ==================================================================== */

static net_tx_entry_t tx_ring[NET_TX_RING_SIZE];

/* head = next slot to write  (producer) */
static volatile int tx_head  = 0;

/* tail = next slot to read   (consumer) */
static volatile int tx_tail  = 0;

/* current occupancy */
static volatile int tx_count = 0;

static spinlock_t tx_lock = SPINLOCK_INIT;

/* ---- Lifetime counters ---- */
static volatile uint64_t stat_enqueued = 0;
static volatile uint64_t stat_flushed  = 0;
static volatile uint64_t stat_dropped  = 0;

/* ====================================================================
 *  Lifecycle
 * ==================================================================== */

void net_tx_init(void)
{
    tx_head  = 0;
    tx_tail  = 0;
    tx_count = 0;

    stat_enqueued = 0;
    stat_flushed  = 0;
    stat_dropped  = 0;

    for (int i = 0; i < NET_TX_RING_SIZE; i++) {
        tx_ring[i].dev = NULL;
        tx_ring[i].nb  = NULL;
    }

    uart_puts("[net_tx] TX ring ready (");
    uart_putu(NET_TX_RING_SIZE);
    uart_puts(" slots, ");
    uart_putu((uint64_t)(NET_TX_RING_SIZE * sizeof(net_tx_entry_t)));
    uart_puts(" B)\n");
}

/* ====================================================================
 *  Producer: enqueue
 * ==================================================================== */

int net_tx_enqueue(net_device_t *dev, netbuf_t *nb)
{
    uint64_t flags;
    spin_lock_irqsave(&tx_lock, &flags);

    if (tx_count >= NET_TX_RING_SIZE) {
        /* Ring full — drop and count */
        if (dev) dev->tx_errors++;
        stat_dropped++;
        spin_unlock_irqrestore(&tx_lock, &flags);
        return -1;
    }

    tx_ring[tx_head].dev = dev;
    tx_ring[tx_head].nb  = nb;

    /* Bitmask wrap — works because NET_TX_RING_SIZE is a power of two */
    tx_head = (tx_head + 1) & (NET_TX_RING_SIZE - 1);
    tx_count++;
    stat_enqueued++;

    spin_unlock_irqrestore(&tx_lock, &flags);
    return 0;
}

/* ====================================================================
 *  Consumer: flush
 * ==================================================================== */

int net_tx_flush(void)
{
    int flushed = 0;

    for (;;) {
        /*
         * Dequeue one entry while holding the lock.
         * Copy the entry out before releasing so the ring slot is
         * immediately available to the producer again.
         */
        net_tx_entry_t entry;
        uint64_t flags;

        spin_lock_irqsave(&tx_lock, &flags);

        if (tx_count == 0) {
            spin_unlock_irqrestore(&tx_lock, &flags);
            break;
        }

        entry = tx_ring[tx_tail];
        tx_ring[tx_tail].dev = NULL;
        tx_ring[tx_tail].nb  = NULL;
        tx_tail = (tx_tail + 1) & (NET_TX_RING_SIZE - 1);
        tx_count--;

        spin_unlock_irqrestore(&tx_lock, &flags);

        /*
         * Transmit the frame outside the lock.
         * net_device_transmit() runs the driver's ops->send() which may
         * take several microseconds for a real NIC — we must not hold
         * tx_lock during this or we'd starve the enqueue path.
         *
         * net_device_transmit() updates tx_packets / tx_bytes / tx_errors
         * inside the device stats.  We free the netbuf unconditionally
         * since we no longer own it either way.
         */
        if (entry.dev && entry.nb) {
            net_device_transmit(entry.dev, entry.nb);
            netbuf_free(entry.nb);
            stat_flushed++;
        }

        flushed++;
    }

    return flushed;
}

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

int net_tx_pending(void)
{
    return tx_count;   /* single int read — atomic on x86-64 */
}

void net_tx_print_stats(void)
{
    uart_puts("[net_tx] ring stats: pending=");
    uart_putu((uint64_t)tx_count);
    uart_puts("  enqueued=");
    uart_putu(stat_enqueued);
    uart_puts("  flushed=");
    uart_putu(stat_flushed);
    uart_puts("  dropped=");
    uart_putu(stat_dropped);
    uart_puts("\n");
}
