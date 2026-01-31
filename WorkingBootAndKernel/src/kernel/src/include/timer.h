#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "idt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIT_BASE_FREQ 1193182UL
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

#ifdef __cplusplus
}
#endif

#endif /* TIMER_H */
