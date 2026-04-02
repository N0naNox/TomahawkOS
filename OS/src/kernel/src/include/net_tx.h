/**
 * @file net_tx.h
 * @brief Network TX packet path — deferred-transmit ring buffer
 *
 * ══════════════════════════════════════════════════════════════════════
 *  TX DEFERRED PATH OVERVIEW
 * ══════════════════════════════════════════════════════════════════════
 *
 *   Higher protocol layers  (UDP / ICMP / ARP / raw socket)
 *     │
 *     │  Option A — synchronous (default):
 *     │    ethernet_send_frame()  →  ops->send()   [existing direct path]
 *     │
 *     │  Option B — deferred (use in ISR or when NIC is busy):
 *     ▼
 *   net_tx_enqueue(dev, nb)
 *     │   ISR-safe: spin_lock_irqsave, O(1) ring push
 *     │
 *     ▼
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │  TX ring  (NET_TX_RING_SIZE slots of {dev, nb})              │
 *   │  spinlock-protected FIFO                                     │
 *   └──────────────────────────────────────────────────────────────┘
 *     │
 *     │  Drained by timer_irq_handler()  every PIT tick:
 *     ▼
 *   net_tx_flush()
 *     └─ while ring non-empty:
 *           ├─ dequeue one {dev, nb}
 *           ├─ net_device_transmit(dev, nb)   ← driver ops->send()
 *           └─ netbuf_free(nb)
 *
 * ══════════════════════════════════════════════════════════════════════
 *  When to use the deferred path
 * ══════════════════════════════════════════════════════════════════════
 *
 *  Use net_tx_enqueue() instead of ethernet_send_frame() when:
 *   • You are inside an ISR / top-half and cannot tolerate a blocking
 *     ops->send() call (e.g. sending an ICMP unreachable from the RX
 *     bottom-half context).
 *   • The NIC TX queue is momentarily full and you want to retry on
 *     the next timer tick instead of dropping the frame.
 *   • You need symmetric buffering: just as the RX path queues frames
 *     in net_rx,  the TX path queues frames here before draining.
 *
 *  Do NOT use it for loopback traffic — the loopback driver is
 *  synchronous and its RX tests assume that sending immediately causes
 *  a visible receive.  Call ethernet_send_frame() directly for lo.
 *
 * ══════════════════════════════════════════════════════════════════════
 *  Design goals (mirrors net_rx.h)
 * ══════════════════════════════════════════════════════════════════════
 *   • Enqueue is O(1) with minimal lock-hold time.
 *   • Flush runs with interrupts re-enabled so ops->send() may sleep.
 *   • Single global ring — all NICs share it (simplest possible model).
 *   • Power-of-two ring size → bitmask wrap, no division.
 * ══════════════════════════════════════════════════════════════════════
 */

#ifndef NET_TX_H
#define NET_TX_H

#include <stdint.h>

/* Forward declarations */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Tunables
 * ==================================================================== */

/**
 * @brief Number of slots in the global TX deferred ring.
 *
 * Must be a power of two.
 * 64 slots × (2 pointers) = 1 KB of ring metadata.
 * TX bursts are usually shorter than RX bursts, so 64 is plenty.
 */
#define NET_TX_RING_SIZE  64

/* ====================================================================
 *  Lifecycle
 * ==================================================================== */

/**
 * @brief Initialise the global TX deferred ring and diagnostic counters.
 *
 * Must be called once during net_init(), before any device is registered
 * and before interrupts are enabled.
 */
void net_tx_init(void);

/* ====================================================================
 *  Producer API  (safe to call from any context, including ISR)
 * ==================================================================== */

/**
 * @brief Enqueue one outgoing frame for deferred transmission.
 *
 * The frame will be sent on the next net_tx_flush() call (timer tick).
 * This function is ISR-safe: it acquires a spinlock with IRQ save/restore.
 *
 * Ownership of @p nb is transferred to the ring on success.  The
 * caller must NOT free or reuse @p nb after a successful enqueue.
 * On failure (-1) the caller retains ownership and should free @p nb.
 *
 * @param dev  NIC to send on (must be registered and link-up).
 * @param nb   netbuf with a complete Ethernet frame  (data points at
 *             the Ethernet header, len covers the full frame).
 * @return  0 on success, -1 if the ring is full (frame dropped).
 */
int net_tx_enqueue(struct net_device *dev, struct netbuf *nb);

/* ====================================================================
 *  Consumer API  (called from timer tick / kernel context)
 * ==================================================================== */

/**
 * @brief Drain the TX ring and transmit every queued frame.
 *
 * Pops entries one at a time (each pop is spinlock-protected), then
 * calls net_device_transmit(dev, nb) outside the lock so that the
 * NIC driver's send() runs with interrupts enabled.
 *
 * After each transmit the netbuf is freed back to the pool regardless
 * of whether the send succeeded (a failed send is counted as tx_errors
 * inside net_device_transmit).
 *
 * Intended call site: timer_irq_handler() in timer.c — symmetric with
 * net_rx_process(), giving TX frames a bounded latency of one PIT period.
 *
 * @return Number of frames flushed during this call (0 if ring was empty).
 */
int net_tx_flush(void);

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

/**
 * @brief Return the number of frames currently queued in the TX ring.
 */
int net_tx_pending(void);

/**
 * @brief Print TX ring statistics (enqueued / flushed / dropped) to UART.
 */
void net_tx_print_stats(void);

#endif /* NET_TX_H */
