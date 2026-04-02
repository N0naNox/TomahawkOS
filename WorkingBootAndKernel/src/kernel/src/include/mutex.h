#pragma once
/**
 * @file mutex.h
 * @brief Kernel mutex — currently implemented as a spinlock + IRQ-disable.
 *
 * The interface intentionally mirrors a sleepable mutex so that the
 * implementation can be upgraded to a scheduler-aware blocking mutex
 * (using scheduler_block_current / scheduler_wake) without touching
 * any call sites.
 *
 * Current implementation notes:
 *   - Suitable for kernel-internal critical sections where the lock is
 *     held for a short, bounded time and the holder never sleeps.
 *   - Interrupts are disabled for the duration of the lock to prevent
 *     deadlock against interrupt handlers that use the same lock.
 *   - The saved RFLAGS are stored inside the mutex_t so callers do not
 *     need to manage them explicitly.
 *
 * Usage:
 *   mutex_t my_mutex = MUTEX_INIT;
 *
 *   mutex_lock(&my_mutex);
 *   // ... critical section ...
 *   mutex_unlock(&my_mutex);
 */

#include "spinlock.h"
#include <stdint.h>

typedef struct {
    spinlock_t  lock;        /**< Underlying spinlock */
    uint64_t    saved_flags; /**< RFLAGS saved by mutex_lock */
} mutex_t;

/** Static initialiser */
#define MUTEX_INIT  { .lock = SPINLOCK_INIT, .saved_flags = 0 }

/**
 * @brief Acquire the mutex (disables interrupts and spins until free).
 */
static inline void mutex_lock(mutex_t *m)
{
    spin_lock_irqsave(&m->lock, &m->saved_flags);
}

/**
 * @brief Release the mutex (restores interrupt state saved by mutex_lock).
 */
static inline void mutex_unlock(mutex_t *m)
{
    spin_unlock_irqrestore(&m->lock, &m->saved_flags);
}
