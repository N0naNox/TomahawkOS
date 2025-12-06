/*
 * idt.c - x86_64 IDT initialization and dispatch
 */

#include "include/idt.h"
#include "include/hal_port_io.h"
#include <string.h>
#include <stdint.h>

#define IDT_ENTRIES 256

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* 64-bit IDT entry (16 bytes) */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

static interrupt_handler_t interrupt_handlers[IDT_ENTRIES];

extern void idt_flush(void* idt_ptr);

/* ISR/IRQ stubs provided by idt_asm.asm */
extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void);
extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void);
extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void); extern void irq1(void); extern void irq2(void); extern void irq3(void);
extern void irq4(void); extern void irq5(void); extern void irq6(void); extern void irq7(void);
extern void irq8(void); extern void irq9(void); extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

/* Helper to set an IDT entry */
static void set_idt_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = selector;
    idt[n].ist = ist & 0x07;
    idt[n].type_attr = type_attr;
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

/* PIC remap to avoid conflicts with CPU exceptions */
static void pic_remap(void) {
    hal_outb(0x20, 0x11);
    hal_outb(0xA0, 0x11);
    hal_outb(0x21, 0x20);
    hal_outb(0xA1, 0x28);
    hal_outb(0x21, 0x04);
    hal_outb(0xA1, 0x02);
    hal_outb(0x21, 0x01);
    hal_outb(0xA1, 0x01);
    hal_outb(0x21, 0x0);
    hal_outb(0xA1, 0x0);
}

void isr_common_handler(regs_t* r) {

    uart_puts("INTERRUPT FIRED!\n");


    if (r->int_no < IDT_ENTRIES && interrupt_handlers[r->int_no]) {
        interrupt_handlers[r->int_no](r);
    } else {
        /* No handler: simple halt for now */
        (void)r;
        for(;;) { __asm__ volatile("hlt"); }
    }
}

void register_interrupt_handler(int n, interrupt_handler_t h) {
    if (n >= 0 && n < IDT_ENTRIES) interrupt_handlers[n] = h;
}

void unregister_interrupt_handler(int n) {
    if (n >= 0 && n < IDT_ENTRIES) interrupt_handlers[n] = 0;
}

void idt_install(void) {
    memset(idt, 0, sizeof(idt));
    idtp.limit = (uint16_t)(sizeof(struct idt_entry) * IDT_ENTRIES - 1);
    idtp.base = (uint64_t)&idt;

    /* Exceptions 0..31 */
    set_idt_gate(0,  (uint64_t)isr0,  0x08, 0x8E, 0);
    set_idt_gate(1,  (uint64_t)isr1,  0x08, 0x8E, 0);
    set_idt_gate(2,  (uint64_t)isr2,  0x08, 0x8E, 0);
    set_idt_gate(3,  (uint64_t)isr3,  0x08, 0x8E, 0);
    set_idt_gate(4,  (uint64_t)isr4,  0x08, 0x8E, 0);
    set_idt_gate(5,  (uint64_t)isr5,  0x08, 0x8E, 0);
    set_idt_gate(6,  (uint64_t)isr6,  0x08, 0x8E, 0);
    set_idt_gate(7,  (uint64_t)isr7,  0x08, 0x8E, 0);
    set_idt_gate(8,  (uint64_t)isr8,  0x08, 0x8E, 0);
    set_idt_gate(9,  (uint64_t)isr9,  0x08, 0x8E, 0);
    set_idt_gate(10, (uint64_t)isr10, 0x08, 0x8E, 0);
    set_idt_gate(11, (uint64_t)isr11, 0x08, 0x8E, 0);
    set_idt_gate(12, (uint64_t)isr12, 0x08, 0x8E, 0);
    set_idt_gate(13, (uint64_t)isr13, 0x08, 0x8E, 0);
    set_idt_gate(14, (uint64_t)isr14, 0x08, 0x8E, 0);
    set_idt_gate(15, (uint64_t)isr15, 0x08, 0x8E, 0);
    set_idt_gate(16, (uint64_t)isr16, 0x08, 0x8E, 0);
    set_idt_gate(17, (uint64_t)isr17, 0x08, 0x8E, 0);
    set_idt_gate(18, (uint64_t)isr18, 0x08, 0x8E, 0);
    set_idt_gate(19, (uint64_t)isr19, 0x08, 0x8E, 0);
    set_idt_gate(20, (uint64_t)isr20, 0x08, 0x8E, 0);
    set_idt_gate(21, (uint64_t)isr21, 0x08, 0x8E, 0);
    set_idt_gate(22, (uint64_t)isr22, 0x08, 0x8E, 0);
    set_idt_gate(23, (uint64_t)isr23, 0x08, 0x8E, 0);
    set_idt_gate(24, (uint64_t)isr24, 0x08, 0x8E, 0);
    set_idt_gate(25, (uint64_t)isr25, 0x08, 0x8E, 0);
    set_idt_gate(26, (uint64_t)isr26, 0x08, 0x8E, 0);
    set_idt_gate(27, (uint64_t)isr27, 0x08, 0x8E, 0);
    set_idt_gate(28, (uint64_t)isr28, 0x08, 0x8E, 0);
    set_idt_gate(29, (uint64_t)isr29, 0x08, 0x8E, 0);
    set_idt_gate(30, (uint64_t)isr30, 0x08, 0x8E, 0);
    set_idt_gate(31, (uint64_t)isr31, 0x08, 0x8E, 0);

    /* Remap PIC and install IRQs 32..47 */
    pic_remap();

    set_idt_gate(32, (uint64_t)irq0,  0x08, 0x8E, 0);
    set_idt_gate(33, (uint64_t)irq1,  0x08, 0x8E, 0);
    set_idt_gate(34, (uint64_t)irq2,  0x08, 0x8E, 0);
    set_idt_gate(35, (uint64_t)irq3,  0x08, 0x8E, 0);
    set_idt_gate(36, (uint64_t)irq4,  0x08, 0x8E, 0);
    set_idt_gate(37, (uint64_t)irq5,  0x08, 0x8E, 0);
    set_idt_gate(38, (uint64_t)irq6,  0x08, 0x8E, 0);
    set_idt_gate(39, (uint64_t)irq7,  0x08, 0x8E, 0);
    set_idt_gate(40, (uint64_t)irq8,  0x08, 0x8E, 0);
    set_idt_gate(41, (uint64_t)irq9,  0x08, 0x8E, 0);
    set_idt_gate(42, (uint64_t)irq10, 0x08, 0x8E, 0);
    set_idt_gate(43, (uint64_t)irq11, 0x08, 0x8E, 0);
    set_idt_gate(44, (uint64_t)irq12, 0x08, 0x8E, 0);
    set_idt_gate(45, (uint64_t)irq13, 0x08, 0x8E, 0);
    set_idt_gate(46, (uint64_t)irq14, 0x08, 0x8E, 0);
    set_idt_gate(47, (uint64_t)irq15, 0x08, 0x8E, 0);

    /* Load IDT */
    idt_flush(&idtp);
}
