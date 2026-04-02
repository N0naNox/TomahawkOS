; entry.asm
; x86-64 kernel entry point
; Sets up basic CPU state and calls kernel_main

bits 64
section .text

; kernel_main is defined in kernel.c
extern kernel_main

; Entry point symbol
global _start
global kernel_start

kernel_start:
_start:
    ; Disable interrupts immediately
    cli
    
    ; At this point, UEFI has:
    ; - rdi = pointer to Boot_Info structure (first argument)
    ; - RSP = some UEFI stack (may be invalid)
    ; - Set up 64-bit mode and enabled paging
    
    ; Set up kernel stack
    ; The stack top is defined in the linker script
    extern stack_top
    lea rsp, [rel stack_top]
    
    ; Boot info pointer is already in rdi (first argument per System V AMD64 ABI)
    ; Just call kernel_main(boot_info)
    call kernel_main
    
    ; If kernel_main returns, halt
    hlt
    jmp $
