/*
 * timer.c - PIT driver + timer IRQ handler for preemption
 */

#include "include/timer.h"
#include "include/idt.h"
#include "include/hal_port_io.h" /* hal_outb, hal_inb */
#include <stdint.h>

/* PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

/* PIT ports */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

/* PIT mode 3 (square wave), access mode lobyte/hibyte */
#define PIT_CMD_BINARY      0x00
#define PIT_CMD_MODE3       0x06
#define PIT_CMD_LOHI        0x30
#define PIT_CMD_CHANNEL0    0x00

/* Exposed globals */
volatile uint64_t timer_ticks = 0;
volatile int need_resched = 0;

/* Configure PIT to desired frequency (TIMER_HZ) */
void pit_init(void) {
    uint32_t divisor = PIT_BASE_FREQ / TIMER_HZ;
    if (divisor == 0) divisor = 1;

    /* Build command: channel 0, access lo/hi, mode 3, binary */
    uint8_t cmd = (PIT_CMD_CHANNEL0) | (PIT_CMD_LOHI) | (PIT_CMD_MODE3) | (PIT_CMD_BINARY);

    hal_outb(PIT_COMMAND, cmd);

    uint8_t lo = (uint8_t)(divisor & 0xFF);
    uint8_t hi = (uint8_t)((divisor >> 8) & 0xFF);

    /* send low byte then high byte */
    hal_outb(PIT_CHANNEL0, lo);
    hal_outb(PIT_CHANNEL0, hi);
}

/* send EOI to PIC after handling IRQ0 */
static inline void pic_send_eoi(void) {
    hal_outb(PIC1_CMD, PIC_EOI);
}

/* Unmask IRQ0 on master PIC (clear bit0 of PIC1_DATA) */
static void enable_irq0_master_pic(void) {
    uint8_t mask = 0xFF;
    /* read current mask -- we don't have hal_inb for PIC port? if you do: */
#ifdef HAL_HAS_INB
    mask = hal_inb(PIC1_DATA);
#else
    /* If no read function available, assume default 0xFF and write mask with IRQ0 enabled */
    /* careful: if other IRQs must remain masked you'll need to maintain mask state in software */
    mask = 0xFF;
#endif
    mask &= ~(1 << 0); /* clear bit 0 to enable IRQ0 */
    hal_outb(PIC1_DATA, mask);
}

/* Timer IRQ handler - called from IDT stub with regs pointer */
void timer_irq_handler(regs_t* r) {
    (void)r;

    /* increment tick counter */
    timer_ticks++;

    /* acknowledge to PIC */
    pic_send_eoi();

    /* request scheduler to run (preemption) */
    need_resched = 1;

    /* Optionally, call the scheduler directly from IRQ context:
     * - define TIMER_CALL_SCHEDULE_DIRECTLY in your build if your scheduler and
     *   context-switching code are interrupt-safe and can be called directly.
     */
#ifdef TIMER_CALL_SCHEDULE_DIRECTLY
    extern void schedule(void); /* ensure prototype */
    schedule();
#endif
}

/* Install the handler and enable PIT */
void timer_install(void) {
    /* initialize PIT hardware */
    pit_init();

    /* enable IRQ0 on master PIC so PIT IRQs are delivered */
    enable_irq0_master_pic();

    /* register handler for vector 32 (IRQ0 after PIC remap) */
    /* Your project probably maps PIC 0..15 to IDT vectors 32..47 */
    register_interrupt_handler(32, timer_irq_handler);
}
