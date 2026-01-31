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
    
    ; Save return value
    mov r15, rax
    
    ; RSP points to regs_t: [rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15, int_no, err_code, rip, cs, rflags, rsp, ss]
    ; Restore most registers (preserving r15 for return value, rcx for RIP, r11 for RFLAGS)
    mov rax, [rsp + 0]      ; rax (will be overwritten with return value)
    mov rbx, [rsp + 8]      ; rbx
    ; skip rcx at [rsp + 16] - will load from rip field
    mov rdx, [rsp + 24]     ; rdx
    mov rsi, [rsp + 32]     ; rsi
    mov rdi, [rsp + 40]     ; rdi
    mov rbp, [rsp + 48]     ; rbp
    mov r8,  [rsp + 56]     ; r8
    mov r9,  [rsp + 64]     ; r9
    mov r10, [rsp + 72]     ; r10
    ; skip r11 at [rsp + 80] - will load from rflags field
    mov r12, [rsp + 88]     ; r12
    mov r13, [rsp + 96]     ; r13
    mov r14, [rsp + 104]    ; r14
    ; skip r15 at [rsp + 112] - using for return value
    
    ; Load RCX (RIP) and R11 (RFLAGS) for SYSRETQ
    mov rcx, [rsp + 136]    ; rip field (after int_no=128, err_code=136... wait that's wrong)
    ; Offset: 15 GPRs * 8 = 120, + int_no (8) = 128, + err_code (8) = 136, rip is at 136
    mov rcx, [rsp + 136]    ; rip
    mov r11, [rsp + 152]    ; rflags (rip + 8 for cs + 8)
    
    ; Load user RSP into scratch
    mov rax, [rsp + 160]    ; user rsp (after rip=136, cs=144, rflags=152)
    mov [gs:0x10], rax
    
    ; Restore return value
    mov rax, r15
    
    ; Discard regs_t structure
    add rsp, 176            ; 22 qwords * 8
    
    ; Build IRETQ frame on current kernel stack
    ; IRETQ pops: RIP, CS, RFLAGS, RSP, SS
    push qword 0x1B         ; SS (user data)
    push qword [gs:0x10]    ; User RSP (still in GS scratch)
    push r11                ; RFLAGS
    push qword 0x23         ; CS (user code)
    push rcx                ; RIP
    
    swapgs
    iretq
