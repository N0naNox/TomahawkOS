#include "syscall.h"

#define SYS_YIELD 1

void sys_yield(void) {
    __asm__ volatile (
        "mov %0, %%rax \n"
        "syscall       \n"
        :
        : "i"(SYS_YIELD)
        : "rax", "rcx", "r11", "memory"
    );
}
