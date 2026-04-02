[bits 64]

global gdt_reload_segments
global jump_to_user

section .text

gdt_reload_segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret

; jump_to_user(uint64_t entry_point, uint64_t user_stack)
; RDI = entry, RSI = stack
jump_to_user:
    cli
    
    ; Load user data segments BEFORE IRETQ
    mov ax, 0x1B  ; User data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Build IRETQ frame: [SS] [RSP] [RFLAGS] [CS] [RIP]
    push qword 0x1B    ; SS
    push rsi           ; RSP
    push qword 0x202   ; RFLAGS
    push qword 0x23    ; CS
    push rdi           ; RIP
    
    ; Clear all registers
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    iretq
