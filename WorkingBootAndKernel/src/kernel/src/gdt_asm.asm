[bits 64]

global gdt_reload_segments
global jump_to_user

section .text

gdt_reload_segments:
    mov ax, 0x10      ; Kernel Data Selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    push 0x08         ; Kernel Code selector
    lea rax, [rel .reload_cs]
    push rax
    retfq             
.reload_cs:
    ret

; jump_to_user(uint64_t entry_point, uint64_t user_stack)
jump_to_user:
    ; rdi = entry_point (RIP)
    ; rsi = user_stack  (RSP)

    cli               ; חובה לבטל אינטררפטים לפני החלפת סגמנטים

    ; טעינת סגמנטים של User Mode (אינדקס 3 + RPL 3 = 0x1B)
    mov ax, 0x1B      ; <--- שונה מ-0x23 ל-0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; הערה: SS ייטען אוטומטית על ידי ה-IRETQ מהמחסנית

    ; הכנת ה-Stack Frame עבור IRETQ
    push 0x1B           ; SS (User Data: 0x18 | 3) <--- שונה מ-0x23 ל-0x1B
    push rsi            ; RSP (User Stack)
    push 0x202          ; RFLAGS
    push 0x23           ; CS (User Code: 0x20 | 3) <--- שונה מ-0x1B ל-0x23
    push rdi          ; RIP (Entry Point)

    ; ניקוי רגיסטרים כדי שהמשתמש לא יראה "שאריות" מהקרנל
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rbp, rbp
    ; אנחנו לא נוגעים ב-rdi, rsi כי כבר דחפנו אותם למחסנית
    
    iretq