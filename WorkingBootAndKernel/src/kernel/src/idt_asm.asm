; idt_asm.asm - NASM/Intel IDT stubs (FIXED stack cleanup)
; - System V AMD64 ABI (arg in RDI)
; - Provides: idt_flush, isr0..isr31, irq0..irq15

BITS 64
SECTION .text

extern isr_common_handler

global idt_flush
; exception handlers
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

; IRQ handlers
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

; ---------------------------
; idt_flush(ptr)
; - Loads IDT from [rdi]
; - Enables interrupts (PIC masking is done in C via pic_remap)
; ---------------------------
idt_flush:
    cli
    lidt [rdi]
    sti
    ret

; ---------------------------
; ISR_NOERR: no error code from CPU
; Stack after all pushes:
;   [rsp+0]:   r15
;   ...
;   [rsp+112]: rax
;   [rsp+120]: int_no
;   [rsp+128]: dummy error (0)
;   [rsp+136]: rip (CPU pushed)
;   [rsp+144]: cs
;   [rsp+152]: rflags
;   [rsp+160]: rsp
;   [rsp+168]: ss
; ---------------------------
%macro ISR_NOERR 1
isr%1:
    push qword 0        ; dummy error code
    push qword %1       ; interrupt number

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call isr_common_handler

    ; restore registers (reverse push order: rax was pushed last)
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; skip int_no and err_code
    add rsp, 16
    iretq
%endmacro

; ---------------------------
; ISR_ERR: CPU pushed error code
; Stack after CPU exception:
;   [rsp+0]: error_code (CPU pushed)
;   [rsp+8]: rip
;   [rsp+16]: cs
;   ...
; After our pushes:
;   [rsp+0]:   r15
;   ...
;   [rsp+112]: rax
;   [rsp+120]: int_no
;   [rsp+128]: error_code (CPU pushed)
;   [rsp+136]: rip
; ---------------------------
%macro ISR_ERR 1
isr%1:
    push qword %1       ; interrupt number (error code already on stack)

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call isr_common_handler

    ; restore registers (reverse push order)
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; skip int_no and cpu-pushed err_code
    add rsp, 16
    iretq
%endmacro

; ---------------------------
; IRQ stubs with EOI
; ---------------------------
%macro IRQ_STUB 1
irq%1:
    push qword 0
    push qword (32 + %1)

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call isr_common_handler

    ; send PIC EOI(s) while registers are still saved
    %if %1 >= 8
        mov al, 0x20
        out 0xA0, al
    %endif
    mov al, 0x20
    out 0x20, al

    ; restore registers (reverse push order)
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; remove int_no and err_code
    add rsp, 16

    iretq
%endmacro

; Exceptions 0..31 (error code on: 8,10,11,12,13,14,17)
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; IRQs 0..15
IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15