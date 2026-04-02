; context_switch(old, new)
; rdi = old cpu_context_t*, rsi = new cpu_context_t*
; cpu_context_t layout:
;   0: rip
;   8: rsp
;  16: rbp
;  24: rbx
;  32: r12
;  40: r13
;  48: r14
;  56: r15

    global context_switch
    default rel

section .text
context_switch:
    ; save callee-saved registers into *old
    mov [rdi + 24], rbx
    mov [rdi + 16], rbp
    mov [rdi + 32], r12
    mov [rdi + 40], r13
    mov [rdi + 48], r14
    mov [rdi + 56], r15

    ; save return rip (on stack) into old->rip
    mov rax, [rsp]
    mov [rdi + 0], rax

    ; save rsp into old->rsp
    mov [rdi + 8], rsp

    ; load registers from new
    mov rbx, [rsi + 24]
    mov rbp, [rsi + 16]
    mov r12, [rsi + 32]
    mov r13, [rsi + 40]
    mov r14, [rsi + 48]
    mov r15, [rsi + 56]

    ; load rsp from new and jump to saved rip
    mov rsp, [rsi + 8]
    mov rax, [rsi + 0]
    jmp rax
