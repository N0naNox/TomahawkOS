#pragma once
/**
 * @file spinlock.h
 * @brief x86-64 spinlock and IRQ-safe locking primitives
 *
 * spinlock_t is a simple test-and-set lock implemented with LOCK XCHG.
 * On a uniprocessor kernel the IRQ-save variants (spin_lock_irqsave /
 * spin_unlock_irqrestore) are sufficient to guarantee mutual exclusion
 * against both interrupt handlers and other kernel threads; on SMP the
 * underlying XCHG provides the cross-CPU guarantee.
 *
 * Usage:
 *   spinlock_t my_lock = SPINLOCK_INIT;
 *
 *   // Short critical section (interrupts already off, or preemption disabled)
 *   spin_lock(&my_lock);
 *   ...
 *   spin_unlock(&my_lock);
 *
 *   // Critical section that must protect against interrupt handlers
 *   uint64_t flags;
 *   spin_lock_irqsave(&my_lock, &flags);
 *   ...
 *   spin_unlock_irqrestore(&my_lock, &flags);
 */

#include <stdint.h>

typedef volatile uint32_t spinlock_t;

/** Static initialiser — lock starts in the unlocked state */
#define SPINLOCK_INIT  0u

/* ---- Implemented in spinlock.asm ---- */

/**
 * @brief Acquire the spinlock, spinning until it is free.
 * Does NOT modify the interrupt flag.
 */
void spin_lock(spinlock_t *lock);

/**
 * @brief Release the spinlock.
 * Does NOT modify the interrupt flag.
 */
void spin_unlock(spinlock_t *lock);

/* ---- Implemented in spinlock.c ---- */

/**
 * @brief Disable interrupts, save RFLAGS, then acquire the spinlock.
 * @param lock  Spinlock to acquire.
 * @param flags Output: caller-saved RFLAGS value to pass to
 *              spin_unlock_irqrestore.
 */
void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags);

/**
 * @brief Release the spinlock then restore RFLAGS (re-enabling interrupts
 *        if they were enabled before the matching spin_lock_irqsave call).
 * @param lock  Spinlock to release.
 * @param flags RFLAGS value returned by the matching spin_lock_irqsave.
 */
void spin_unlock_irqrestore(spinlock_t *lock, uint64_t *flags);
