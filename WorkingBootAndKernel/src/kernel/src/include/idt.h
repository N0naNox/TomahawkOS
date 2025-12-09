/*
 * idt.h - x86_64 Interrupt Descriptor Table interface
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Register/state snapshot passed to C interrupt handlers.
 * Layout matches the stack frame constructed by assembly stubs.
 * Stack grows DOWN, so first push is at highest address.
 */
typedef struct regs {
    /* Pushed by our assembly (in reverse order from pushes) */
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t int_no;
    uint64_t err_code;

    /* Pushed by CPU on interrupt entry */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} regs_t;

typedef void (*interrupt_handler_t)(regs_t* r);

void idt_install(void);
void register_interrupt_handler(int n, interrupt_handler_t h);
void unregister_interrupt_handler(int n);

/* Called from assembly with a pointer to `struct regs` */
void isr_common_handler(regs_t* r);

#endif /* IDT_H */