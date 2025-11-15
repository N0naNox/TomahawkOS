; halt.asm
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

PUBLIC cpu_halt

cpu_halt PROC
.hlt_loop:
    hlt
    jmp .hlt_loop
cpu_halt ENDP

END