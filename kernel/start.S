.global _start
.type _start, @function
_start:
    mov %rdi, %rdi    // magic
    mov %rsi, %rsi    // fb
    mov %rdx, %rdx    // pitch
    call kernel_main
    hlt
