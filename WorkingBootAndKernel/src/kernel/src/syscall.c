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
extern volatile uint64_t usermode_demo_return_rip;
extern volatile int usermode_demo_completed;

/* Extended syscall handler with more arguments */
uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, regs_t* regs) 
{
    switch(syscall_num) {
        case SYSCALL_TEST:
            /* Test syscall from usermode demo - we're back in Ring 0! */
            vga_write("\nSyscall received from Ring 3!\n");
            vga_write("Ring 3 -> Ring 0 transition successful!\n");
            vga_write("\n=== User Mode Demo Complete! ===\n");
            vga_write("Press ESC to return to menu.\n");
            
            /* Restore kernel data segments - but NOT GS, it's used for syscall cpu_info */
            __asm__ volatile(
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                ::: "ax"
            );
            
            /* Poll keyboard directly for ESC - keep timer masked */
            while (1) {
                if (hal_inb(0x64) & 0x01) {
                    uint8_t scancode = hal_inb(0x60);
                    if (scancode == 0x01) {  /* ESC scancode */
                        break;
                    }
                }
                __asm__ volatile("pause");
            }
            
            /* Flush any remaining scancodes from keyboard controller */
            while (hal_inb(0x64) & 0x01) {
                hal_inb(0x60);  /* Discard */
            }
            
            /* Restore timer interrupt AND ensure keyboard IRQ is unmasked */
            uint8_t pic_mask = hal_inb(0x21);
            pic_mask &= ~0x01;  /* Unmask IRQ0 (timer) */
            pic_mask &= ~0x02;  /* Unmask IRQ1 (keyboard) */
            hal_outb(0x21, pic_mask);
            
            /* Re-enable interrupts */
            __asm__ volatile("sti");
            
            /* Mark demo as completed */
            usermode_demo_completed = 1;
            
            /* CRITICAL: We're bypassing the normal syscall return path which does swapgs.
             * We must do swapgs here to restore GS bases to the correct state,
             * otherwise the next syscall will fail because GS won't point to cpu_info. */
            __asm__ volatile("swapgs");
            
            /* Jump back to saved context in run_usermode_demo */
            __asm__ volatile(
                "mov %0, %%rsp\n"
                "mov %1, %%rbp\n"
                "jmp *%2\n"
                :
                : "r"(usermode_demo_return_rsp), "r"(usermode_demo_return_rbp), "r"(usermode_demo_return_rip)
            );
            
            /* Never reached */
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