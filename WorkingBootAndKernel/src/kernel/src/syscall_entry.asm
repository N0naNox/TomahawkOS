extern syscall_handler_c
global syscall_entry

syscall_entry:
    swapgs          ; החלפת ה-GS לגרסת הקרנל (כדי להגיע למחסנית הקרנל/TSS)
    mov [gs:0x10], rsp ; שמירת מחסנית המשתמש (נניח שב-offset 0x10 ב-GS נמצא ה-scratch)
    mov rsp, [gs:0x00] ; טעינת מחסנית הקרנל

    ; שמירת רגיסטרים שה-C עשוי לדרוס
    push r11        ; r11 שומר את ה-RFLAGS הישן
    push rcx        ; rcx שומר את ה-RIP הישן
    push rdi
    push rsi
    push rdx
    push r10        ; ב-syscall, r10 מחליף את rcx כארגומנט רביעי

    ; --- סידור ארגומנטים עבור syscall_handler_c(num, arg1) ---
    ; אנחנו רוצים ש:
    ; RDI = מספר הסיסקול (נמצא ב-RAX)
    ; RSI = הארגומנט הראשון (נמצא ב-RDI המקורי של המשתמש)
    
    mov rsi, rdi    ; מעבירים את ה-arg1 (המחרוזת) ל-RSI
    mov rdi, rax    ; מעבירים את מספר הסיסקול ל-RDI


    call syscall_handler_c

    ; RAX now contains return value - don't touch it!
    
    ; שחזור
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rcx
    pop r11
    
    mov rsp, [gs:0x10] ; חזרה למחסנית המשתמש

    swapgs


    sysretq         ; חזרה ל-User Mode!