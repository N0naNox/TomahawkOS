/*
 * idt.h - x86_64 Interrupt Descriptor Table interface
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Register/state snapshot passed to C interrupt handlers.
 * Layout matches the stack frame constructed by the assembly stubs in idt_asm.asm.
 * Assembly pushes (from high to low addresses): err_code, int_no, r15..rax (r15 first, rax last), then CPU frame (rip, cs, rflags, rsp, ss).
 * RSP handed to isr_common_handler points at rax (lowest address among saved GPRs).
 */
typedef struct regs {
    /* General purpose registers (rax is at lowest address) */
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

    /* Interrupt info */
    uint64_t int_no;
    uint64_t err_code;

    /* Values pushed by CPU on interrupt entry (before our stub runs) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} regs_t;

typedef void (*interrupt_handler_t)(regs_t* r);

void idt_install(void);

/* Reload IDT with higher-half base once higher-half mapping is active. */
void idt_reload_high(void);
void register_interrupt_handler(int n, interrupt_handler_t h);
void unregister_interrupt_handler(int n);

/* Poll latest interrupt info (returns 1 if new) */
int idt_read_last_int(uint64_t* int_no, uint64_t* err_code, uint64_t* seq_out);

/* Called from assembly with a pointer to `struct regs` */
void isr_common_handler(regs_t* r);

#endif /* IDT_H */