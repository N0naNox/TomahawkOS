#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "include/proc.h"
#include "include/signal.h"
#include "include/sys_proc.h"
#include "include/idt.h"
#include "include/hal_port_io.h"
#include "include/keyboard.h"
#include "uart.h"

/* External saved context from usermode demo */
extern volatile uint64_t usermode_demo_return_rsp;
extern volatile uint64_t usermode_demo_return_rbp;
extern volatile int usermode_demo_completed;

/* Extended syscall handler with more arguments */
uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, regs_t* regs) 
{
    switch(syscall_num) {
        case SYSCALL_TEST:
            /* Test syscall from usermode demo - we're back in Ring 0! */
            vga_write("\nSyscall received from Ring 3!\n");
            vga_write("Ring 3 -> Ring 0 transition successful!\n");
            
            /* Restore timer interrupt */
            uint8_t pic_mask = hal_inb(0x21);
            hal_outb(0x21, pic_mask & ~0x01);  /* Unmask IRQ0 */
            
            /* Restore kernel data segments */
            __asm__ volatile(
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                "mov %%ax, %%gs\n"
                ::: "ax"
            );
            
            vga_write("\n=== User Mode Demo Complete! ===\n");
            vga_write("Returning to menu in 3 seconds...\n");
            
            /* Wait ~3 seconds so user can see the result */
            for (volatile int i = 0; i < 30000000; i++) {
                __asm__ volatile("pause");
            }
            
            /* Mark demo as completed and restore saved context */
            usermode_demo_completed = 1;
            
            /* Jump back to saved context in run_usermode_demo */
            __asm__ volatile(
                "mov %0, %%rsp\n"
                "mov %1, %%rbp\n"
                :
                : "r"(usermode_demo_return_rsp), "r"(usermode_demo_return_rbp)
            );
            
            /* Never reached - we jumped away */
            return 0;

        case SYS_YIELD:
            scheduler_yield();
            return 0;

        case SYS_EXIT:
            return sys_exit();

        case SYS_FORK:
            return sys_fork();

        case SYS_GETPID:
            return sys_getpid();

        case SYS_SIGNAL:
            return sys_signal((int)arg1, (sig_handler_t)arg2);

        case SYS_KILL:
            return sys_kill(arg1, (int)arg2);

        case SYS_SIGRETURN:
            return (uint64_t)signal_return(get_current_process(), regs);

        default:
            uart_puts("[KERNEL] Unknown syscall\n");
            return (uint64_t)-1;
    }
}