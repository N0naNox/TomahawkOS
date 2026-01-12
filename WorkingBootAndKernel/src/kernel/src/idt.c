/*
 * idt.c - x86_64 IDT initialization and dispatch
 */

#include "include/idt.h"
#include "include/hal_port_io.h"
#include <string.h>
#include <stdint.h>
#include <uart.h>

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

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

/* Lightweight interrupt log for post-ISR consumption in the main loop */
static volatile uint64_t log_seq = 0;
static volatile uint64_t log_int_no = 0;
static volatile uint64_t log_err = 0;

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

/* Forward declaration of page fault handler */
extern int page_fault_handler(uint64_t error_code, uint64_t faulting_address);

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
    
    /* Mask all IRQs except IRQ1 (keyboard) */
    hal_outb(0x21, 0xFD);  /* 11111101 - only IRQ1 unmasked */
    hal_outb(0xA1, 0xFF);  /* All slave IRQs masked */
}

void isr_common_handler(regs_t* r) {
    /* Record last interrupt for main-loop polling */
    log_int_no = r->int_no;
    log_err = r->err_code;
    log_seq++;

    if (r->int_no < IDT_ENTRIES && interrupt_handlers[r->int_no]) {
        interrupt_handlers[r->int_no](r);
    } else {
        /* No registered handler; return to caller */
    }
}

/* Wrapper for page fault handler (vector 14) to extract CR2 and call the C handler */
static void page_fault_wrapper(regs_t* r) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    uart_puts("#PF err=");
    uart_putu(r->err_code);
    uart_puts(" cr2=");
    uart_putu(cr2);
    uart_puts("\n");
    page_fault_handler(r->err_code, cr2);
}

void register_interrupt_handler(int n, interrupt_handler_t h) {
    if (n >= 0 && n < IDT_ENTRIES) interrupt_handlers[n] = h;
}

void unregister_interrupt_handler(int n) {
    if (n >= 0 && n < IDT_ENTRIES) interrupt_handlers[n] = 0;
}


static void gpf_handler(regs_t* r) {
    uart_puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    uart_puts("EXCEPTION: GENERAL PROTECTION FAULT (13)\n");
    uart_puts("WE ARE OFFICIALLY IN USER MODE!\n");
    uart_puts("Error Code: ");
    uart_putu(r->err_code);
    uart_puts("\nRIP: ");
    uart_putu(r->rip);  // הכתובת המדויקת שגרמה לשגיאה
    uart_puts("\nCS: ");
    uart_putu(r->cs);   // כאן תוכל לראות אם זה קרה ב-Ring 3 (ערך 0x1B או 0x23)
    uart_puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    // אנחנו לא יכולים להמשיך מ-GPF בדרך כלל, אז עוצרים
    while(1) {
        __asm__ volatile("hlt");
    }
}


/* Retrieve last interrupt log; returns 1 if new data was read */
int idt_read_last_int(uint64_t* int_no, uint64_t* err_code, uint64_t* seq_out) {
    static uint64_t last_seq = 0;
    if (log_seq == last_seq) return 0;
    last_seq = log_seq;
    if (int_no) *int_no = log_int_no;
    if (err_code) *err_code = log_err;
    if (seq_out) *seq_out = log_seq;
    return 1;
}

void idt_install(void) {
    memset(idt, 0, sizeof(idt));
    idtp.limit = (uint16_t)(sizeof(struct idt_entry) * IDT_ENTRIES - 1);
    idtp.base = (uint64_t)&idt;

    /* Use the current code segment selector instead of a hardcoded 0x08. */
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    /* Exceptions 0..31 */
    set_idt_gate(0,  (uint64_t)isr0,  cs, 0x8E, 0);
    set_idt_gate(1,  (uint64_t)isr1,  cs, 0x8E, 0);
    set_idt_gate(2,  (uint64_t)isr2,  cs, 0x8E, 0);
    set_idt_gate(3,  (uint64_t)isr3,  cs, 0x8E, 0);
    set_idt_gate(4,  (uint64_t)isr4,  cs, 0x8E, 0);
    set_idt_gate(5,  (uint64_t)isr5,  cs, 0x8E, 0);
    set_idt_gate(6,  (uint64_t)isr6,  cs, 0x8E, 0);
    set_idt_gate(7,  (uint64_t)isr7,  cs, 0x8E, 0);
    set_idt_gate(8,  (uint64_t)isr8,  cs, 0x8E, 0);
    set_idt_gate(9,  (uint64_t)isr9,  cs, 0x8E, 0);
    set_idt_gate(10, (uint64_t)isr10, cs, 0x8E, 0);
    set_idt_gate(11, (uint64_t)isr11, cs, 0x8E, 0);
    set_idt_gate(12, (uint64_t)isr12, cs, 0x8E, 0);
    set_idt_gate(13, (uint64_t)isr13, cs, 0x8E, 0);
    set_idt_gate(14, (uint64_t)isr14, cs, 0x8E, 0);
    set_idt_gate(15, (uint64_t)isr15, cs, 0x8E, 0);
    set_idt_gate(16, (uint64_t)isr16, cs, 0x8E, 0);
    set_idt_gate(17, (uint64_t)isr17, cs, 0x8E, 0);
    set_idt_gate(18, (uint64_t)isr18, cs, 0x8E, 0);
    set_idt_gate(19, (uint64_t)isr19, cs, 0x8E, 0);
    set_idt_gate(20, (uint64_t)isr20, cs, 0x8E, 0);
    set_idt_gate(21, (uint64_t)isr21, cs, 0x8E, 0);
    set_idt_gate(22, (uint64_t)isr22, cs, 0x8E, 0);
    set_idt_gate(23, (uint64_t)isr23, cs, 0x8E, 0);
    set_idt_gate(24, (uint64_t)isr24, cs, 0x8E, 0);
    set_idt_gate(25, (uint64_t)isr25, cs, 0x8E, 0);
    set_idt_gate(26, (uint64_t)isr26, cs, 0x8E, 0);
    set_idt_gate(27, (uint64_t)isr27, cs, 0x8E, 0);
    set_idt_gate(28, (uint64_t)isr28, cs, 0x8E, 0);
    set_idt_gate(29, (uint64_t)isr29, cs, 0x8E, 0);
    set_idt_gate(30, (uint64_t)isr30, cs, 0x8E, 0);
    set_idt_gate(31, (uint64_t)isr31, cs, 0x8E, 0);

    /* Register page fault handler wrapper */
    register_interrupt_handler(14, page_fault_wrapper);


    /* הוסף את זה: רישום הטיפול בשגיאת הגנה כללית */
    register_interrupt_handler(13, gpf_handler);

    /* Remap PIC and install IRQs 32..47 */
    pic_remap();

    set_idt_gate(32, (uint64_t)irq0,  cs, 0x8E, 0);
    set_idt_gate(33, (uint64_t)irq1,  cs, 0x8E, 0);
    set_idt_gate(34, (uint64_t)irq2,  cs, 0x8E, 0);
    set_idt_gate(35, (uint64_t)irq3,  cs, 0x8E, 0);
    set_idt_gate(36, (uint64_t)irq4,  cs, 0x8E, 0);
    set_idt_gate(37, (uint64_t)irq5,  cs, 0x8E, 0);
    set_idt_gate(38, (uint64_t)irq6,  cs, 0x8E, 0);
    set_idt_gate(39, (uint64_t)irq7,  cs, 0x8E, 0);
    set_idt_gate(40, (uint64_t)irq8,  cs, 0x8E, 0);
    set_idt_gate(41, (uint64_t)irq9,  cs, 0x8E, 0);
    set_idt_gate(42, (uint64_t)irq10, cs, 0x8E, 0);
    set_idt_gate(43, (uint64_t)irq11, cs, 0x8E, 0);
    set_idt_gate(44, (uint64_t)irq12, cs, 0x8E, 0);
    set_idt_gate(45, (uint64_t)irq13, cs, 0x8E, 0);
    set_idt_gate(46, (uint64_t)irq14, cs, 0x8E, 0);
    set_idt_gate(47, (uint64_t)irq15, cs, 0x8E, 0);

    /* Load IDT */
    idt_flush(&idtp);
}

void idt_reload_high(void) {
    /* Adjust IDT base to its higher-half alias and reload. */
    idtp.base = ((uint64_t)&idt) + KERNEL_VIRT_BASE;
    idt_flush(&idtp);
}
