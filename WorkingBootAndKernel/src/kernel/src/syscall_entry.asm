global syscall_entry
extern syscall_dispatch

section .text
syscall_entry:
    ; rax = syscall number

    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    mov rdi, rax          ; arg0 = syscall number
    call syscall_dispatch

    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    sysretq
