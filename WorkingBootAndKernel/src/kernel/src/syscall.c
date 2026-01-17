#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "include/proc.h"
#include "include/signal.h"
#include "include/idt.h"
#include "uart.h"

/* Extended syscall handler with more arguments */
uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, regs_t* regs) 
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

        case SYS_SIGNAL: {
            /* sys_signal(signo, handler) */
            int signo = (int)arg1;
            sig_handler_t handler = (sig_handler_t)arg2;
            
            uart_puts("[KERNEL] sys_signal(");
            uart_putu(signo);
            uart_puts(", 0x");
            uart_puthex((uint64_t)handler);
            uart_puts(")\n");
            
            pcb_t* proc = get_current_process();
            if (!proc) return (uint64_t)-1;
            
            if (signo <= 0 || signo >= NSIG) {
                uart_puts("sys_signal: invalid signal number\n");
                return (uint64_t)-1;
            }
            
            /* Cannot catch SIGKILL or SIGSTOP */
            if (signo == SIGKILL || signo == SIGSTOP) {
                uart_puts("sys_signal: cannot catch SIGKILL or SIGSTOP\n");
                return (uint64_t)-1;
            }
            
            /* Save old handler and install new one */
            sig_handler_t old_handler = proc->signals.handlers[signo];
            proc->signals.handlers[signo] = handler;
            
            uart_puts("sys_signal: installed handler for signal ");
            uart_putu(signo);
            uart_puts("\n");
            
            return (uint64_t)old_handler;
        }

        case SYS_KILL: {
            /* sys_kill(pid, signo) */
            uint64_t target_pid = arg1;
            int signo = (int)arg2;
            
            uart_puts("[KERNEL] sys_kill(");
            uart_putu(target_pid);
            uart_puts(", ");
            uart_putu(signo);
            uart_puts(")\n");
            
            if (signo <= 0 || signo >= NSIG) {
                uart_puts("sys_kill: invalid signal number\n");
                return (uint64_t)-1;
            }
            
            pcb_t* target = find_process_by_pid(target_pid);
            if (!target) {
                uart_puts("sys_kill: process not found\n");
                return (uint64_t)-1;
            }
            
            int result = signal_send(target, signo);
            return (uint64_t)result;
        }

        case SYS_SIGPROCMASK: {
            /* sys_sigprocmask(how, set, oldset) */
            int how = (int)arg1;
            sigset_t* set = (sigset_t*)arg2;
            sigset_t* oldset = (sigset_t*)arg3;
            
            pcb_t* proc = get_current_process();
            if (!proc) return (uint64_t)-1;
            
            /* Save old mask if requested */
            if (oldset) {
                *oldset = proc->signals.blocked;
            }
            
            /* Modify mask based on 'how' */
            if (set) {
                switch (how) {
                    case 0: /* SIG_BLOCK */
                        proc->signals.blocked |= *set;
                        break;
                    case 1: /* SIG_UNBLOCK */
                        proc->signals.blocked &= ~(*set);
                        break;
                    case 2: /* SIG_SETMASK */
                        proc->signals.blocked = *set;
                        break;
                    default:
                        return (uint64_t)-1;
                }
            }
            
            return 0;
        }

        case SYS_SIGRETURN: {
            uart_puts("[KERNEL] sys_sigreturn()\n");
            
            pcb_t* proc = get_current_process();
            if (!proc || !regs) return (uint64_t)-1;
            
            int result = signal_return(proc, regs);
            return (uint64_t)result;
        }

        default:
            uart_puts("[KERNEL] Unknown syscall: ");
            uart_putu(syscall_num);
            uart_puts("\n");
            return (uint64_t)-1;
    }
}
