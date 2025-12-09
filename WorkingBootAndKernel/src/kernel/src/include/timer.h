#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "idt.h" /* for regs_t */

#ifdef __cplusplus
extern "C" {
#endif

/* PIT base frequency (Hz) */
#define PIT_BASE_FREQ 1193182UL

/* default ticks per second (choose 100 => 10ms tick) */
#ifndef TIMER_HZ
#define TIMER_HZ 100
#endif

/* Initialize PIT to generate TIMER_HZ interrupts */
void pit_init(void);

/* Install timer interrupt handler and enable IRQ line */
void timer_install(void);

/* IRQ handler called from IDT/IRQ stubs */
void timer_irq_handler(regs_t* r);

/* number of ticks since boot */
extern volatile uint64_t timer_ticks;

/* flag set by handler when a reschedule is required */
extern volatile int need_resched;

#ifdef __cplusplus
}
#endif

#endif /* TIMER_H */
