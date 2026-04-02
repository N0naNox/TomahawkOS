/**
 * @file spinlock.c
 * @brief x86-64 spinlock and IRQ-save locking primitives
 *
 * spin_lock and spin_unlock use LOCK XCHG to implement a test-and-set
 * spinlock.  spin_lock_irqsave / spin_unlock_irqrestore additionally
 * save and restore RFLAGS so that interrupt handlers that try to acquire
 * the same lock cannot deadlock the CPU.
 */

#include "include/spinlock.h"

/*
 * spin_lock — acquire the spinlock, spinning until it is free.
 *
 * The LOCK XCHG atomically swaps 1 into the lock word.
 * If the old value was already 1 (locked) we spin with a PAUSE hint
 * (reduces memory traffic and avoids memory-ordering speculation penalties)
 * until the lock *looks* free before retrying the atomic exchange.
 */
void spin_lock(spinlock_t *lock)
{
    __asm__ volatile (
        "mov    $1, %%eax       \n"   /* EAX = 1 (desired locked value) */
        "1:                    \n"
        "xchg   %%eax, (%0)    \n"   /* atomic: EAX <-> [lock] */
        "test   %%eax, %%eax   \n"   /* was the lock free (old == 0)? */
        "jz     3f             \n"   /* yes -> we own it, done */
        /* Lock was held.  Spin on a plain read until it looks free. */
        "2:                    \n"
        "pause                 \n"   /* pipeline hint */
        "mov    (%0), %%eax    \n"   /* non-atomic read for hysteresis */
        "test   %%eax, %%eax   \n"
        "jnz    2b             \n"   /* still locked -> keep waiting */
        /* Looks free now: reload EAX = 1 and retry the atomic exchange. */
        "mov    $1, %%eax      \n"
        "jmp    1b             \n"
        "3:                    \n"
        :
        : "r" (lock)
        : "eax", "memory"
    );
}

/*
 * spin_unlock — release the spinlock.
 *
 * A plain aligned 32-bit store is sequentially consistent on x86, so no
 * LOCK prefix is required.  The "memory" clobber prevents the compiler
 * from reordering stores across spin_unlock.
 */
void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile (
        "movl   $0, (%0) \n"
        :
        : "r" (lock)
        : "memory"
    );
}

void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags)
{
    uint64_t saved_flags;

    /* Snapshot RFLAGS then disable interrupts atomically */
    __asm__ volatile (
        "pushfq           \n"
        "popq  %0         \n"
        "cli              \n"
        : "=r" (saved_flags)
        :
        : "memory"
    );

    *flags = saved_flags;

    /* Now acquire the lock — no IRQ can interrupt us on this CPU */
    spin_lock(lock);
}

void spin_unlock_irqrestore(spinlock_t *lock, uint64_t *flags)
{
    /* Release the lock first, then restore interrupt state */
    spin_unlock(lock);

    __asm__ volatile (
        "pushq %0         \n"
        "popfq            \n"
        :
        : "r" (*flags)
        : "memory"
    );
}
