    .text
    .globl switch_context
    .type switch_context, @function
/* switch_context(cpu_context_t* old, cpu_context_t* new)
 *
 * Layout of cpu_context_t (must match proc.h):
 * 0: rip (8)
 * 8: rsp (8)
 * 16: rbp (8)
 * 24: rbx (8)
 * 32: r12 (8)
 * 40: r13 (8)
 * 48: r14 (8)
 * 56: r15 (8)
 */
switch_context:
    /* rdi = old, rsi = new */

    /* --- save callee-saved registers into *old --- */
    movq %rbx, 24(%rdi)        /* store rbx */
    movq %rbp, 16(%rdi)        /* store rbp */
    movq %r12, 32(%rdi)        /* store r12 */
    movq %r13, 40(%rdi)        /* store r13 */
    movq %r14, 48(%rdi)        /* store r14 */
    movq %r15, 56(%rdi)        /* store r15 */

    /* save rsp */
    movq %rsp, 8(%rdi)

    /* save rip: return address is at (%rsp) */
    movq (%rsp), %rax
    movq %rax, 0(%rdi)

    /* --- load callee-saved registers from *new --- */
    movq 24(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 32(%rsi), %r12
    movq 40(%rsi), %r13
    movq 48(%rsi), %r14
    movq 56(%rsi), %r15

    /* restore rsp from new */
    movq 8(%rsi), %rsp

    /* push new RIP and return */
    movq 0(%rsi), %rax
    pushq %rax
    ret
