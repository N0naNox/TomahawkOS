.section .text
.global _start_entry
_start_entry:
    /* Set RSP to a safe, high temporary stack (8MB) */
    mov $0x800000, %rsp
    
    /* Call the C main function defined in kernel.c */
    call kernel_main

    /* If kernel_main returns, halt */
    cli
    hlt