/* scheduler.h - Basic cooperative round-robin scheduler API */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "proc.h"
#include "idt.h"

/* Initialize scheduler */
void scheduler_init(void);

/* Add a newly created thread to the ready queue */
void scheduler_add_thread(tcb_t* t);

/* Yield the CPU from current thread to next ready thread */
void scheduler_yield(void);

/* Timer tick handler: preempt current thread if another is ready */
void scheduler_tick(regs_t* r);

/* Return pointer to current running thread */
tcb_t* scheduler_current(void);

/* Called when a thread exits to pick next thread (not implemented here) */
void scheduler_thread_exit(void);

#endif /* SCHEDULER_H */
