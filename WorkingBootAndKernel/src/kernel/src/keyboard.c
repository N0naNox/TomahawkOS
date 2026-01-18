/* keyboard.c - Minimal PS/2 keyboard driver using scancode set 1
 * - Registers IRQ1 handler (vector 33 after PIC remap)
 * - Reads scancode from port 0x60 using HAL
 * - Maps common scancodes to ASCII (lowercase)
 * - Echoes characters to VGA and serial
 */

#include "include/keyboard.h"
#include "include/hal_port_io.h"
#include "include/idt.h"
#include "include/vga.h"
#include <uart.h>

/* Simple ring buffer */
static volatile char kb_buf[128];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

/* Basic scancode set 1 to ASCII map (0..0x7F). 0 means unmapped */
static const char scancode_map[128] = {
    /* 0x00 - 0x0F */
    0,  0, '1','2','3','4','5','6','7','8','9','0','-','=', 0,  0,
    /* 0x10 - 0x1F */
    'q','w','e','r','t','y','u','i','o','p','[',']', '\n', 0, 'a','s',
    /* 0x20 - 0x2F */
    'd','f','g','h','j','k','l', ';','\'', '`', 0, '\\','z','x','c','v',
    /* 0x30 - 0x3F */
    'b','n','m',',','.','/', 0,  '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    /* rest default 0 */
};

/* Shutdown the system */
static void shutdown_system(void) {
    uart_puts("\n\n=== SYSTEM SHUTDOWN (F12 pressed) ===\n");
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("\n\n  System shutting down...\n");
    
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    /* Use QEMU debug exit device to close window */
    hal_outb(0xf4, 0x10);  /* Exit code 0x10 -> actual exit code (0x10 << 1) | 1 = 33 */
    
    /* If QEMU didn't close, halt */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* IRQ handler called from C via isr_common_handler */
static void keyboard_irq_handler(regs_t* r) {
    (void)r;
    /* Read scancode */
    uint8_t sc = hal_inb(0x60);

    /* Check for F12 shutdown (scancode 0x58) */
    if (sc == 0x58) {
        shutdown_system();
        return;
    }

    /* Ignore break codes (high bit set) */
    if (sc & 0x80) {
        return;
    }

    char c = 0;
    if (sc == 0x01) {
        c = 27; /* ESC */
    } else if (sc < sizeof(scancode_map)) {
        c = scancode_map[sc];
    }

    if (c) {
        /* Minimal ISR work: buffer enqueue only */
        int next = (kb_head + 1) & (sizeof(kb_buf)-1);
        if (next != kb_tail) {
            kb_buf[kb_head] = c;
            kb_head = next;
        }
    }

    /* Note: EOI is sent by the IRQ stub in idt_asm.asm */
}

void keyboard_init(void) {
    /* Register IRQ1 handler and unmask keyboard IRQ on the master PIC */
    register_interrupt_handler(32 + 1, keyboard_irq_handler);

    uint8_t mask = hal_inb(0x21);
    mask &= ~(1 << 1); /* clear bit1 to enable IRQ1 */
    hal_outb(0x21, mask);
}

char keyboard_getchar(void) {
    if (kb_tail == kb_head) return 0;
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) & (sizeof(kb_buf)-1);
    return c;
}

/* Polling fallback: check controller status and echo if data ready. */
void keyboard_poll_once(void) {
    /* Status port 0x64, bit0=output buffer full */
    uint8_t st = hal_inb(0x64);
    if ((st & 0x01) == 0) return;

    uint8_t sc = hal_inb(0x60);

    /* Ignore break codes */
    if (sc & 0x80) return;

    char c = 0;
    if (sc < sizeof(scancode_map)) c = scancode_map[sc];
    if (!c) return;

    uart_putchar(c);

    int next = (kb_head + 1) & (sizeof(kb_buf)-1);
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}
