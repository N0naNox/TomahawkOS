/**
 * @file net_rx.h
 * @brief Network RX packet path — ISR-safe ring buffer and bottom-half processor
 *
 * ══════════════════════════════════════════════════════════════════════
 *  RX INTERRUPT PATH OVERVIEW
 * ══════════════════════════════════════════════════════════════════════
 *
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │  TOP HALF  (runs inside hardware IRQ handler, IRQs disabled) │
 *   │                                                              │
 *   │   NIC asserts IRQ                                            │
 *   │     → PIC forwards to IDT vector                            │
 *   │     → isr_common_handler  (idt.c)                           │
 *   │     → net_irq_dispatch()  (net.c)                           │
 *   │          ├─ find net_device_t by vector                     │
 *   │          ├─ netbuf_alloc()                                  │
 *   │          ├─ dev->ops->isr(dev, nb)  ← driver fills nb       │
 *   │          ├─ net_rx_enqueue(dev, nb) ← push to ring (O(1))  │
 *   │          └─ PIC EOI                                         │
 *   └──────────────────────────────────────────────────────────────┘
 *                              │
 *              spinlock-protected ring buffer
 *              (NET_RX_RING_SIZE slots of {dev, nb})
 *                              │
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │  BOTTOM HALF  (deferred — called from timer tick, IRQ0)     │
 *   │                                                              │
 *   │   net_rx_process()                                           │
 *   │     └─ while ring non-empty:                                 │
 *   │           ├─ dequeue one entry                              │
 *   │           ├─ ethernet_receive(dev, nb)  ← full L2/L3/L4     │
 *   │           └─ netbuf_free(nb)                                 │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * Design goals
 * ────────────
 *  • Top half is minimal: no allocation wait, no deep stack usage.
 *  • Bottom half runs with interrupts re-enabled: safe for blocking
 *    operations and scheduling calls inside the demux chain.
 *  • Single global ring so any number of NICs can coexist.
 *  • Power-of-two ring size → modular index arithmetic without division.
 * ══════════════════════════════════════════════════════════════════════
 */

#ifndef NET_RX_H
#define NET_RX_H

#include <stdint.h>

/* Forward declarations (avoids pulling in heavy headers) */
struct net_device;
struct netbuf;

/* ====================================================================
 *  Tunables
 * ==================================================================== */

/**
 * @brief Number of slots in the global RX ring.
 *
 * Must be a power of two (the implementation uses bitmask wrap-around).
 * 128 slots × (2 pointers) = 2 KB of ring metadata.
 * With NETBUF_POOL_SIZE=64, 128 slots means bursts of up to 64 frames
 * can be enqueued before the pool runs dry.
 */
#define NET_RX_RING_SIZE  128

/* ====================================================================
 *  Lifecycle
 * ==================================================================== */

/**
 * @brief Initialise the global RX ring and its diagnostic counters.
 *
 * Must be called once during net_init(), before any device is registered
 * and before interrupts are enabled.
 */
void net_rx_init(void);

/* ====================================================================
 *  Top-half API (called from ISR context)
 * ==================================================================== */

/**
 * @brief Enqueue one received frame for deferred bottom-half processing.
 *
 * This is the only function called from the NIC ISR.  It is designed to
 * be fast and lock-hold time minimal — just two pointer writes and an
 * index increment, protected by a spinlock.
 *
 * If the ring is full the frame is silently dropped and
 * @p dev->rx_errors is incremented.  The caller must NOT free @p nb on
 * a successful enqueue; net_rx_process() owns it from that point on.
 * On failure (-1) the caller should free @p nb itself.
 *
 * @param dev  The NIC the frame arrived on.
 * @param nb   netbuf with data pointing at the raw Ethernet frame.
 * @return  0 on success, -1 if the ring is full (frame dropped).
 */
int net_rx_enqueue(struct net_device *dev, struct netbuf *nb);

/* ====================================================================
 *  Bottom-half API (called from timer tick / kernel thread)
 * ==================================================================== */

/**
 * @brief Drain the RX ring and run the full network demux on each frame.
 *
 * Pops entries from the ring one at a time (each pop is brief and
 * spinlock-protected), then calls ethernet_receive() outside the lock so
 * that the full L2→L3→L4→socket demux path runs with interrupts enabled.
 *
 * Intended call site: timer_irq_handler() in timer.c — runs on every
 * IRQ0 tick so the maximum queuing latency is one PIT period (~1 ms at
 * 1000 Hz, ~10 ms at 100 Hz).
 *
 * @return Number of frames processed during this call (0 if ring was empty).
 */
int net_rx_process(void);

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

/**
 * @brief Return the number of frames currently queued in the ring.
 */
int net_rx_pending(void);

/**
 * @brief Print RX ring statistics (enqueued / processed / dropped) to UART.
 */
void net_rx_print_stats(void);

#endif /* NET_RX_H */
