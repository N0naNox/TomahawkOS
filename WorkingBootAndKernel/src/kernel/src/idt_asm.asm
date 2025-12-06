; idt_asm.asm - NASM/Intel IDT stubs (works with your idt.c / regs_t)
; - System V AMD64 ABI (arg in RDI)
; - Provides: idt_flush, isr0..isr31, irq0..irq15
; - Uses the same push order as your prior GAS stubs so isr_common_handler(regs_t*) works

BITS 64
SECTION .text

extern isr_common_handler

global idt_flush
; exception handlers
global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

; IRQ handlers
global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

; ---------------------------
; idt_flush(ptr)
; - Loads IDT from [rdi]
; - Masks PICs before enabling interrupts to avoid spurious IRQs
; ---------------------------
idt_flush:
    cli
    lidt [rdi]
    ; mask both PICs (all IRQs masked) so enabling interrupts is safe
    mov al, 0xFF
    out 0x21, al     ; master PIC mask
    out 0xA1, al     ; slave  PIC mask
    sti
    ret

; ---------------------------
; Helper macros to build frames
; The push order here mirrors the GAS macros you used earlier:
;  pushq $0          ; dummy error code
;  pushq $num        ; interrupt number
;  push regs: rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15
;  mov rdi, rsp
;  call isr_common_handler
;  add rsp, <cleanup>
;  iretq
; ---------------------------

%macro ISR_NOERR 1
global isr%1
isr%1:
    ; push dummy error code then interrupt number (so C code sees them)
    push qword 0
    push qword %1

    ; save general-purpose registers (order preserved)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; pass pointer to regs frame in RDI and call C handler
    mov rdi, rsp
    call isr_common_handler

    ; cleanup same bytes pushed: 2 (qwords) + 15 regs = (2+15)*8 = 136
    add rsp, 136
    iretq
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    ; For exceptions that have an error code already pushed by CPU,
    ; we make the stack layout match ISR_NOERR (so C always sees same layout).
    ; CPU already pushed error code on stack; we push the interrupt number
    ; and then registers, so the final layout is identical.
    push qword %1   ; push interrupt number

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_common_handler

    ; cleanup: 1 (int_no) + 15 regs + (cpu-pushed error code remains below)
    add rsp, 136    ; same cleanup bytes so stack restored consistently
    iretq
%endmacro

; IRQ stubs: same frame building, but send EOI(s) before iretq.
; We'll send slave EOI for IRQs >=8, then master EOI always.
%macro IRQ_STUB 1
global irq%1
irq%1:
    ; push dummy error code and vector number (32 + num)
    push qword 0
    push qword (32 + %1)

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_common_handler

    ; send PIC EOI(s)
    ; if slave is involved (IRQ >= 8) send EOI to slave first
    %if %1 >= 8
        mov al, 0x20
        out 0xA0, al
    %endif
    mov al, 0x20
    out 0x20, al

    add rsp, 136
    iretq
%endmacro

; Exceptions 0..31 - mark which have error code (8,10,11,12,13,14,17)
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

; end of file
