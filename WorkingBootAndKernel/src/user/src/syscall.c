#include "syscall.h"

#define SYS_YIELD 1

void sys_yield(void) {
    __asm__ volatile (
        "mov $SYS_YIELD, %%rax \n"
        "syscall              \n"
        :
        :
        : "rax", "rcx", "r11", "memory"
    );
}
