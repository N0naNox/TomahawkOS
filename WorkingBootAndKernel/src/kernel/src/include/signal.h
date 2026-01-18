/*
 * signal.h - POSIX-like signal definitions and handling
 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>

/* Standard POSIX signal numbers */
#define SIGHUP      1   /* Hangup */
#define SIGINT      2   /* Interrupt (Ctrl-C) */
#define SIGQUIT     3   /* Quit */
#define SIGILL      4   /* Illegal instruction */
#define SIGTRAP     5   /* Trace/breakpoint trap */
#define SIGABRT     6   /* Abort */
#define SIGBUS      7   /* Bus error */
#define SIGFPE      8   /* Floating point exception */
#define SIGKILL     9   /* Kill (cannot be caught or ignored) */
#define SIGUSR1     10  /* User-defined signal 1 */
#define SIGSEGV     11  /* Segmentation fault */
#define SIGUSR2     12  /* User-defined signal 2 */
#define SIGPIPE     13  /* Broken pipe */
#define SIGALRM     14  /* Alarm clock */
#define SIGTERM     15  /* Termination */
#define SIGSTKFLT   16  /* Stack fault */
#define SIGCHLD     17  /* Child status changed */
#define SIGCONT     18  /* Continue if stopped */
#define SIGSTOP     19  /* Stop (cannot be caught or ignored) */
#define SIGTSTP     20  /* Terminal stop (Ctrl-Z) */
#define SIGTTIN     21  /* Background read from tty */
#define SIGTTOU     22  /* Background write to tty */
#define SIGURG      23  /* Urgent condition on socket */
#define SIGXCPU     24  /* CPU time limit exceeded */
#define SIGXFSZ     25  /* File size limit exceeded */
#define SIGVTALRM   26  /* Virtual alarm clock */
#define SIGPROF     27  /* Profiling alarm clock */
#define SIGWINCH    28  /* Window size change */
#define SIGIO       29  /* I/O now possible */
#define SIGPWR      30  /* Power failure */
#define SIGSYS      31  /* Bad system call */

/* Maximum signal number (for array sizing) */
#define NSIG        32
#define MAX_SIGNAL  31

/* Signal handler types */
#define SIG_DFL     ((sig_handler_t)0)  /* Default action */
#define SIG_IGN     ((sig_handler_t)1)  /* Ignore signal */
#define SIG_ERR     ((sig_handler_t)-1) /* Error return */

/* Signal actions for default handlers */
typedef enum {
    SIG_ACTION_TERM,    /* Terminate process */
    SIG_ACTION_IGN,     /* Ignore signal */
    SIG_ACTION_CORE,    /* Terminate and dump core */
    SIG_ACTION_STOP,    /* Stop process */
    SIG_ACTION_CONT     /* Continue if stopped */
} sig_default_action_t;

/* User-space signal handler function pointer */
typedef void (*sig_handler_t)(int);

/* Signal set (bitmask) */
typedef uint64_t sigset_t;

/* Signal information structure (simplified) */
typedef struct siginfo {
    int si_signo;       /* Signal number */
    int si_code;        /* Signal code */
    int si_pid;         /* Sending process ID */
    int si_uid;         /* Sending user ID */
    void* si_addr;      /* Faulting address (for SIGSEGV, etc.) */
} siginfo_t;

/* Per-process signal state */
typedef struct signal_struct {
    sig_handler_t handlers[NSIG];   /* Signal handlers (per signal) */
    sigset_t pending;                /* Pending signals bitmask */
    sigset_t blocked;                /* Blocked signals bitmask */
    void* sigstack;                  /* Alternate signal stack (optional) */
    uint64_t saved_rip;              /* Saved RIP for signal return */
    uint64_t saved_rsp;              /* Saved RSP for signal return */
    int in_signal_handler;           /* Flag: currently in signal handler */
} signal_struct_t;

/* Initialize signal structure with default handlers */
void signal_init(signal_struct_t* sig);

/* Get default action for a signal */
sig_default_action_t signal_default_action(int signo);

/* Signal set manipulation macros */
#define sigemptyset(set)    (*(set) = 0)
#define sigfillset(set)     (*(set) = ~0ULL)
#define sigaddset(set, sig) (*(set) |= (1ULL << ((sig) - 1)))
#define sigdelset(set, sig) (*(set) &= ~(1ULL << ((sig) - 1)))
#define sigismember(set, sig) ((*(set) & (1ULL << ((sig) - 1))) != 0)

/* Check if a signal is pending and not blocked */
static inline int signal_pending(signal_struct_t* sig)
{
    return (sig->pending & ~sig->blocked) != 0;
}

/* Get the next pending signal (highest priority first) */
int signal_next_pending(signal_struct_t* sig);

/* Add a signal to the pending set */
void signal_queue(signal_struct_t* sig, int signo);

/* Remove a signal from the pending set */
void signal_dequeue(signal_struct_t* sig, int signo);

/* Forward declaration for PCB */
struct pcb;

/* Send signal to a process */
int signal_send(struct pcb* proc, int signo);

/* Install signal handler for a process (kernel helper) */
void signal_install(struct pcb* proc, int signo, uintptr_t handler);

/* Check if process has pending signals */
int signal_has_pending(struct pcb* proc);

/* Deliver pending signals by modifying interrupt frame */
/* Returns 1 if a signal was delivered, 0 otherwise */
struct regs;
int signal_deliver(struct pcb* proc, struct regs* r);

/* Handle signal return from user space */
int signal_return(struct pcb* proc, struct regs* r);

#endif /* SIGNAL_H */
