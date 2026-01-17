#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "uart.h"

void syscall_handler_c(uint64_t syscall_num, uint64_t arg1) 
{
    switch(syscall_num) {

        case  SYSCALL_TEST:
            vga_write("SYSCALL 1: ");
        
            if (arg1 != 0) {
                // הדפסת המחרוזת עצמה
                vga_write((const char*)arg1);
                uart_puts("Content: ");
                uart_puts((const char*)arg1);
                uart_puts("\n");
            }
            break;

        case SYS_YIELD:
            uart_puts("[KERNEL] Process yielded. Scheduling next...\n");
            scheduler_yield();
            return;

        case SYS_EXIT:
            uart_puts("[KERNEL] Process exiting...\n");
            scheduler_thread_exit();
            return;

        default:
            break;
    }
    
}
