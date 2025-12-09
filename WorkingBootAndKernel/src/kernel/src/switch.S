    /* switch.S - save/restore CPU context
     *
     * C signature:
     *   void switch_context(cpu_context_t *old, cpu_context_t *new);
     *
     * cpu_context_t layout (in proc.h):
     *   uint64_t rip;   // offset 0
     *   uint64_t rsp;   // offset 8
     *   uint64_t rbp;   // offset 16
     *   uint64_t rbx;   // offset 24
     *   uint64_t r12;   // offset 32
     *   uint64_t r13;   // offset 40
     *   uint64_t r14;   // offset 48
     *   uint64_t r15;   // offset 56
     *
     * We save callee-saved regs + rip/rsp.
     */

    .text
    .globl switch_context
    .type switch_context, @function

switch_context:
    /* rdi = old, rsi = new */

    /* Save callee-saved registers into *old */
    movq %r15, 56(%rdi)
    movq %r14, 48(%rdi)
    movq %r13, 40(%rdi)
    movq %r12, 32(%rdi)
    movq %rbx, 24(%rdi)
    movq %rbp, 16(%rdi)

    /* Save current RSP and return address (RIP) */
    movq %rsp, 8(%rdi)        /* old->rsp = rsp */
    movq (%rsp), %rax         /* saved return address is on stack top */
    movq %rax, 0(%rdi)        /* old->rip = saved return address */

    /* Load callee-saved registers from *new */
    movq 56(%rsi), %r15
    movq 48(%rsi), %r14
    movq 40(%rsi), %r13
    movq 32(%rsi), %r12
    movq 24(%rsi), %rbx
    movq 16(%rsi), %rbp

    /* Load new rsp and jump to new->rip */
    movq 8(%rsi), %rsp        /* set stack */
    movq 0(%rsi), %rax        /* new->rip into rax */
    jmp *%rax                 /* jump to new RIP (does not return) */

    .size switch_context, .-switch_context
