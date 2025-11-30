.section .text
.p2align 4
.global idt_flush
.type idt_flush, @function
/* idt_flush: load IDT. Expects pointer to idt_ptr in %rdi */
idt_flush:
    cli
    lidt (%rdi)
    sti
    ret

/*
 * Assembly stubs for ISRs/IRQs.
 * For interrupts that DO NOT push an error code, we push a dummy 0 first,
 * then push the interrupt number. For those that DO push an error code,
 * we only push the interrupt number so that the stack layout is uniform
 * when handed to the C handler.
 */

.macro ISR_NOERR num
.globl isr\num
.type isr\num, @function
isr\num:
    pushq $0
    pushq $(\num)
    /* save general purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call isr_common_handler
    /* clean pushed regs + int_no/err_code (15*8 + 16 = 136) */
    addq $136, %rsp
    iretq
.endm

.macro ISR_ERR num
.globl isr\num
.type isr\num, @function
isr\num:
    pushq $(\num)
    /* save general purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call isr_common_handler
    /* clean pushed regs + int_no (15*8 + 8 = 128) */
    addq $128, %rsp
    iretq
.endm

.macro IRQ_STUB num
.globl irq\num
.type irq\num, @function
irq\num:
    /* IRQs don't push error codes; follow NOERR pattern but use vector 32+num */
    pushq $0
    pushq $(32 + \num)
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call isr_common_handler
    addq $136, %rsp
    /* send EOI to PICs - done in C handler if desired; leaving out here */
    iretq
.endm

/* Exceptions 0..31. Some push error code: 8,10,11,12,13,14,17 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* IRQs 0..15 */
IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

.size idt_flush, .-idt_flush
