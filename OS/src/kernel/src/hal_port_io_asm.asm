; hal_port_io.asm
; Minimal NASM-based HAL for x86-64 port I/O
; - Provides low-level primitives used by hal_port_io.c
; - System V AMD64 ABI: arg1 -> rdi (port), arg2 -> rsi (value)
; - Assembles with: nasm -f elf64 hal_port_io.asm

bits 64
section .text

global hal_inb_asm
global hal_outb_asm
global hal_inw_asm
global hal_outw_asm
global hal_inl_asm
global hal_outl_asm

; uint8_t hal_inb_asm(uint16_t port)
hal_inb_asm:
    ; port in DI -> need DX for IN instruction
    mov dx, di
    in al, dx
    ; zero-extend AL into RAX for return
    movzx rax, al
    ret

; void hal_outb_asm(uint16_t port, uint8_t value)
hal_outb_asm:
    ; port in DI, value in RSI (low byte in SIL)
    mov dx, di
    mov al, sil
    out dx, al
    ret

; uint16_t hal_inw_asm(uint16_t port)
hal_inw_asm:
    mov dx, di
    in ax, dx
    movzx rax, ax
    ret

; void hal_outw_asm(uint16_t port, uint16_t value)
hal_outw_asm:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

; uint32_t hal_inl_asm(uint16_t port)
hal_inl_asm:
    mov dx, di
    in eax, dx
    ; in eax, dx already zero-extends into rax on x86-64
    ret

; void hal_outl_asm(uint16_t port, uint32_t value)
hal_outl_asm:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

; void hal_insw_asm(uint16_t port, void *buffer, uint32_t count)
; Reads 'count' 16-bit words from 'port' into 'buffer'.
; SysV ABI: port=rdi, buffer=rsi, count=rdx
; rep insw needs: dx=port, rdi=dest, rcx=count
global hal_insw_asm
hal_insw_asm:
    mov rcx, rdx        ; count -> rcx  (must be BEFORE dx is overwritten)
    mov rdx, rdi        ; port  -> rdx  (only low 16 bits matter)
    mov rdi, rsi        ; buffer -> rdi (destination for rep insw)
    rep insw
    ret

; void hal_outsw_asm(uint16_t port, const void *buffer, uint32_t count)
; Writes 'count' 16-bit words from 'buffer' to 'port'.
; SysV ABI: port=rdi, buffer=rsi, count=rdx
; rep outsw needs: dx=port, rsi=src, rcx=count
global hal_outsw_asm
hal_outsw_asm:
    mov rcx, rdx        ; count -> rcx
    mov rdx, rdi        ; port  -> rdx
    ; rsi already holds buffer (source for rep outsw)
    rep outsw
    ret
