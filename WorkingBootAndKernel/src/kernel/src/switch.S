    .text
    .global context_switch
    .type context_switch, @function
/*
 * context_switch(old, new)
 * rdi = old (cpu_context_t*), rsi = new (cpu_context_t*)
 * cpu_context_t layout (offsets):
 * 0: rip
 * 8: rsp
 * 16: rbp
 * 24: rbx
 * 32: r12
 * 40: r13
 * 48: r14
 * 56: r15
 */
context_switch:
    /* save callee-saved registers into *old */
    movq %rbx, 24(%rdi)
    movq %rbp, 16(%rdi)
    movq %r12, 32(%rdi)
    movq %r13, 40(%rdi)
    movq %r14, 48(%rdi)
    movq %r15, 56(%rdi)

    /* save return rip (on stack) into old->rip */
    movq (%rsp), %rax
    movq %rax, 0(%rdi)

    /* save rsp into old->rsp */
    movq %rsp, 8(%rdi)

    /* load registers from new */
    movq 24(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 32(%rsi), %r12
    movq 40(%rsi), %r13
    movq 48(%rsi), %r14
    movq 56(%rsi), %r15

    /* load rsp from new and jump to saved rip */
    movq 8(%rsi), %rsp
    movq 0(%rsi), %rax
    jmp *%rax

    .size context_switch, .-context_switch
