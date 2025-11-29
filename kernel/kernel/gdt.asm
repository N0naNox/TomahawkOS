; gdt.asm — MASM syntax
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

.data
; nothing

.code

gdt_load_asm PROC
    ; RCX = pointer to GDTPtr
    lgdt [rcx]

    ; Reload segment registers
    mov ax, 0x10      ; data segment selector (index 2)
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Far jump to reload CS
    mov rax, offset .flush
    push 0x08         ; code segment selector (index 1)
    push rax
    retfq

.flush:
    ret
gdt_load_asm ENDP

END