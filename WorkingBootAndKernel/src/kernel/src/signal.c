/*
 * signal.c - Signal handling implementation
 */

#include "include/signal.h"
#include <string.h>

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
