#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "include/proc.h"
#include "uart.h"

uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1) 
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
            return 0;

        case SYS_YIELD:
            uart_puts("[KERNEL] Process yielded. Scheduling next...\n");
            scheduler_yield();
            return 0;

        case SYS_EXIT:
            uart_puts("[KERNEL] Process exiting...\n");
            scheduler_thread_exit();
            return 0;  /* Never reached */

        case SYS_FORK: {
            uart_puts("[KERNEL] fork() called\n");
            int child_pid = fork_process();
            uart_puts("[KERNEL] fork() returning ");
            uart_putu(child_pid);
            uart_puts("\n");
            return (uint64_t)child_pid;
        }

        case SYS_GETPID: {
            pcb_t* proc = get_current_process();
            if (proc) {
                return (uint64_t)proc->pid;
            }
            return 0;
        }

        default:
            uart_puts("[KERNEL] Unknown syscall: ");
            uart_putu(syscall_num);
            uart_puts("\n");
            return (uint64_t)-1;
    }
}
