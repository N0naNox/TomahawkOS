extern syscall_handler_c
global syscall_entry

syscall_entry:
    swapgs          ; 
    mov [gs:0x10], rsp ; 
    mov rsp, [gs:0x00] ; 

    ; Build a regs_t structure on the stack for signal delivery
    ; regs_t layout (see idt.h): rax, rbx, rcx, rdx, rsi, rdi, rbp, r8-r15, int_no, err_code, rip, cs, rflags, rsp, ss
    
    ; Save user SS and RSP (from GS scratch)
    push qword 0x1B         ; SS (user data segment)
    push qword [gs:0x10]    ; User RSP
    push r11                ; RFLAGS (saved by syscall)
    push qword 0x23         ; CS (user code segment)
    push rcx                ; RIP (saved by syscall)
    
    push qword 0            ; err_code (not used for syscall)
    push qword 0x80         ; int_no (0x80 for syscall marker)
    
    ; Save GPRs (r15 down to rax)
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

    ; Now RSP points to a complete regs_t structure
    
    ; Call handler: syscall_handler_c(num, arg1, arg2, arg3, regs)
    ; System V ABI: RDI, RSI, RDX, RCX, R8, R9
    mov r8, rsp     ; regs_t* as 5th arg
    mov rcx, r10    ; arg3 (r10 is used instead of rcx in syscall)
    ; rdx already has arg2
    ; rsi already has arg1  
    mov rdi, rax    ; syscall number
    
    call syscall_handler_c
    
    ; Restore registers from regs_t (in case signal handler modified them)
    pop rbx         ; skip rax (return value in rax)
    mov rbx, rax    ; save return value
    pop rax         ; skip old rax
    pop rax         ; skip rcx
    pop rax         ; skip rdx
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
    
    add rsp, 16     ; skip int_no, err_code
    
    pop rcx         ; RIP
    add rsp, 8      ; skip CS
    pop r11         ; RFLAGS
    pop rax         ; User RSP
    mov [gs:0x10], rax  ; Store back to scratch
    add rsp, 8      ; skip SS
    
    mov rax, rbx    ; restore return value
    
    mov rsp, [gs:0x10] ; 

    swapgs

    sysretq         ; 
