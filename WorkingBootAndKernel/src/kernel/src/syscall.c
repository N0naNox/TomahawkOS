#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"

void syscall_handler_c(uint64_t syscall_num, uint64_t arg1) {

    vga_write("!!!!SYSCALL PRINT!!!!");
    uart_puts("!!!!SYSCALL PRINT!!!!");


    if (syscall_num == 1) { // נניח ש-1 זה PRINT
        vga_write((const char*)arg1);
        uart_puts("Syscall Print: ");
        uart_puts((const char*)arg1);
    }
}
