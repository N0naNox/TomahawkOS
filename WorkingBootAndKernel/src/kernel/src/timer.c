#include "timer.h"
#include "idt.h"
#include "hal_port_io.h"
#include <stdint.h>
#include <uart.h>

/* PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC_EOI   0x20

/* PIT ports */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_CMD_BINARY      0x00
#define PIT_CMD_MODE3       0x06
#define PIT_CMD_LOHI        0x30
#define PIT_CMD_CHANNEL0    0x00

volatile uint64_t timer_ticks = 0;

/* PIT init */
void pit_init(void) {
    uint32_t divisor = PIT_BASE_FREQ / TIMER_HZ;
    if (divisor == 0) divisor = 1;

    uint8_t cmd = PIT_CMD_CHANNEL0 | PIT_CMD_LOHI | PIT_CMD_MODE3 | PIT_CMD_BINARY;
    hal_outb(PIT_COMMAND, cmd);

    hal_outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    hal_outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Send EOI to PIC after IRQ0 */
static inline void pic_send_eoi(void) {
    hal_outb(PIC1_CMD, PIC_EOI);
}

/* Enable IRQ0 on master PIC */
static void enable_irq0_master_pic(void) {
    uint8_t mask = hal_inb(PIC1_DATA);
    mask &= ~(1 << 0); // enable IRQ0
    hal_outb(PIC1_DATA, mask);
}

/* Timer IRQ handler just counts ticks */
void timer_irq_handler(regs_t* r) {
    (void)r;
    timer_ticks++;
    if (timer_ticks == 1) {
        uart_puts("timer: first tick\n");
    }
}

/* Install timer */
void timer_install(void) {
    pit_init();

    /* register handler BEFORE unmasking IRQ line to avoid race */
    register_interrupt_handler(32, timer_irq_handler);

    /* unmask IRQ0 on master PIC */
    enable_irq0_master_pic();

    /* leave CPU interrupts disabled here -- caller should enable with sti
       only after all installs (IDT, devices, handlers) are done. */
}
