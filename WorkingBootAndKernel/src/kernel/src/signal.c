/*
 * signal.c - Signal handling implementation
 */

#include "include/signal.h"
#include "include/proc.h"
#include "include/idt.h"
#include "include/scheduler.h"
#include <string.h>
#include <uart.h>

/* Default actions for each signal (POSIX-compliant) */
static const sig_default_action_t default_actions[NSIG] = {
    [0]         = SIG_ACTION_IGN,   /* No signal 0 */
    [SIGHUP]    = SIG_ACTION_TERM,
    [SIGINT]    = SIG_ACTION_TERM,
    [SIGQUIT]   = SIG_ACTION_CORE,
    [SIGILL]    = SIG_ACTION_CORE,
    [SIGTRAP]   = SIG_ACTION_CORE,
    [SIGABRT]   = SIG_ACTION_CORE,
    [SIGBUS]    = SIG_ACTION_CORE,
    [SIGFPE]    = SIG_ACTION_CORE,
    [SIGKILL]   = SIG_ACTION_TERM,  /* Cannot be caught */
    [SIGUSR1]   = SIG_ACTION_TERM,
    [SIGSEGV]   = SIG_ACTION_CORE,
    [SIGUSR2]   = SIG_ACTION_TERM,
    [SIGPIPE]   = SIG_ACTION_TERM,
    [SIGALRM]   = SIG_ACTION_TERM,
    [SIGTERM]   = SIG_ACTION_TERM,
    [SIGSTKFLT] = SIG_ACTION_TERM,
    [SIGCHLD]   = SIG_ACTION_IGN,
    [SIGCONT]   = SIG_ACTION_CONT,
    [SIGSTOP]   = SIG_ACTION_STOP,  /* Cannot be caught */
    [SIGTSTP]   = SIG_ACTION_STOP,
    [SIGTTIN]   = SIG_ACTION_STOP,
    [SIGTTOU]   = SIG_ACTION_STOP,
    [SIGURG]    = SIG_ACTION_IGN,
    [SIGXCPU]   = SIG_ACTION_CORE,
    [SIGXFSZ]   = SIG_ACTION_CORE,
    [SIGVTALRM] = SIG_ACTION_TERM,
    [SIGPROF]   = SIG_ACTION_TERM,
    [SIGWINCH]  = SIG_ACTION_IGN,
    [SIGIO]     = SIG_ACTION_TERM,
    [SIGPWR]    = SIG_ACTION_TERM,
    [SIGSYS]    = SIG_ACTION_CORE,
};

void signal_init(signal_struct_t* sig)
{
    if (!sig) return;

    /* Set all handlers to default */
    for (int i = 0; i < NSIG; i++) {
        sig->handlers[i] = SIG_DFL;
    }

    /* No signals pending or blocked */
    sig->pending = 0;
    sig->blocked = 0;
    sig->sigstack = NULL;
    sig->in_signal_handler = 0;
    sig->saved_rip = 0;
    sig->saved_rsp = 0;
}

sig_default_action_t signal_default_action(int signo)
{
    if (signo <= 0 || signo >= NSIG) {
        return SIG_ACTION_IGN;
    }
    return default_actions[signo];
}

int signal_next_pending(signal_struct_t* sig)
{
    if (!sig) return 0;

    /* Get unblocked pending signals */
    sigset_t ready = sig->pending & ~sig->blocked;
    
    if (ready == 0) return 0;

    /* Find first set bit (lowest signal number first) */
    for (int i = 1; i < NSIG; i++) {
        if (ready & (1ULL << (i - 1))) {
            return i;
        }
    }

    return 0;
}

void signal_queue(signal_struct_t* sig, int signo)
{
    if (!sig || signo <= 0 || signo >= NSIG) return;
    
    sigaddset(&sig->pending, signo);
}

void signal_dequeue(signal_struct_t* sig, int signo)
{
    if (!sig || signo <= 0 || signo >= NSIG) return;
    
    sigdelset(&sig->pending, signo);
}

/* Send signal to a process */
int signal_send(pcb_t* proc, int signo)
{
    if (!proc || signo <= 0 || signo >= NSIG) return -1;
    
    uart_puts("signal_send: sending signal ");
    uart_putu(signo);
    uart_puts(" to process ");
    uart_putu(proc->pid);
    uart_puts("\n");
    
    /* Check if signal is blocked */
    if (sigismember(&proc->signals.blocked, signo)) {
        uart_puts("signal_send: signal is blocked\n");
        /* Still queue it, will be delivered when unblocked */
    }
    
    signal_queue(&proc->signals, signo);
    return 0;
}

/* Check if process should handle signals */
int signal_has_pending(pcb_t* proc)
{
    if (!proc) return 0;
    return signal_pending(&proc->signals);
}

/* Deliver a pending signal to user mode */
int signal_deliver(pcb_t* proc, regs_t* r)
{
    if (!proc || !r) return 0;
    
    /* Don't deliver signals if already in a signal handler */
    if (proc->signals.in_signal_handler) {
        return 0;
    }
    
    /* Get next pending unblocked signal */
    int signo = signal_next_pending(&proc->signals);
    if (signo == 0) {
        return 0;  /* No pending signals */
    }
    
    uart_puts("signal_deliver: delivering signal ");
    uart_putu(signo);
    uart_puts(" to process ");
    uart_putu(proc->pid);
    uart_puts("\n");
    
    /* Get handler for this signal */
    sig_handler_t handler = proc->signals.handlers[signo];
    
    /* Handle special cases */
    if (handler == SIG_IGN) {
        /* Ignore signal - just dequeue it */
        signal_dequeue(&proc->signals, signo);
        uart_puts("signal_deliver: signal ignored\n");
        return 0;
    }
    
    if (handler == SIG_DFL || handler == NULL) {
        /* Default action */
        sig_default_action_t action = signal_default_action(signo);
        
        switch (action) {
            case SIG_ACTION_IGN:
                signal_dequeue(&proc->signals, signo);
                uart_puts("signal_deliver: default action - ignore\n");
                return 0;
                
            case SIG_ACTION_TERM:
            case SIG_ACTION_CORE:
                uart_puts("signal_deliver: terminating process due to signal ");
                uart_putu(signo);
                uart_puts("\n");
                proc->exit_code = 128 + signo;
                signal_dequeue(&proc->signals, signo);
                scheduler_thread_exit();
                return 1;
                
            case SIG_ACTION_STOP:
                /* TODO: implement process stopping */
                uart_puts("signal_deliver: STOP not implemented\n");
                signal_dequeue(&proc->signals, signo);
                return 0;
                
            case SIG_ACTION_CONT:
                /* TODO: implement process continuation */
                signal_dequeue(&proc->signals, signo);
                return 0;
        }
    }
    
    /* User handler - deliver to user space */
    uart_puts("signal_deliver: calling user handler at 0x");
    uart_puthex((uint64_t)handler);
    uart_puts("\n");
    
    /* Save current execution context */
    proc->signals.saved_rip = r->rip;
    proc->signals.saved_rsp = r->rsp;
    proc->signals.in_signal_handler = 1;
    
    /* Dequeue signal */
    signal_dequeue(&proc->signals, signo);
    
    /* Set up user-space signal handler call */
    /* Push signal number as argument (System V AMD64 ABI: first arg in RDI) */
    r->rdi = signo;
    
    /* Adjust user stack to store return address */
    r->rsp -= 8;
    /* TODO: validate and write return trampoline address to user stack */
    /* For now, we'll use a simple approach - handler must call sys_sigreturn */
    
    /* Jump to user handler */
    r->rip = (uint64_t)handler;
    
    return 1;
}

/* Handle return from signal handler */
int signal_return(pcb_t* proc, regs_t* r)
{
    if (!proc || !r) return -1;
    
    uart_puts("signal_return: returning from signal handler\n");
    
    if (!proc->signals.in_signal_handler) {
        uart_puts("signal_return: not in signal handler!\n");
        return -1;
    }
    
    /* Restore saved context */
    r->rip = proc->signals.saved_rip;
    r->rsp = proc->signals.saved_rsp;
    proc->signals.in_signal_handler = 0;
    
    uart_puts("signal_return: restored RIP=0x");
    uart_puthex(r->rip);
    uart_puts(" RSP=0x");
    uart_puthex(r->rsp);
    uart_puts("\n");
    
    return 0;
}
/* 
 * signal_install - Install a signal handler (kernel helper for demos)
 */
void signal_install(struct pcb* proc, int signo, uintptr_t handler)
{
    if (signo < 1 || signo > MAX_SIGNAL) {
        return;
    }
    
    if (signo == SIGKILL || signo == SIGSTOP) {
        /* Cannot override SIGKILL or SIGSTOP */
        return;
    }
    
    proc->signals.handlers[signo] = (sig_handler_t)handler;
    uart_puts("signal_install: installed handler for signal ");
    uart_putu(signo);
    uart_puts(" at 0x");
    uart_puthex(handler);
    uart_puts("\n");
}