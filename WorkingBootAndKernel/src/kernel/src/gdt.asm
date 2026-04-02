BITS 64
GLOBAL gdt_flush

SECTION .text

gdt_flush:
    lgdt [rdi]

    mov ax, 0x10        ; kernel data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    push 0x08           ; kernel code
    lea rax, [rel .reload]
    push rax
    retfq

.reload:
    ret
