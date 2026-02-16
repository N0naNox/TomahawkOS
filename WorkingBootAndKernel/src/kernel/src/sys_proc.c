#include "sys_proc.h"

/**
 * @brief פונקציית עזר לבדיקת הרשאות בין תהליכים.
 * @return true אם לתהליך המקור יש הרשאה לנהל את תהליך היעד.
 */
static bool can_manage_process(pcb_t* source, pcb_t* target) {
    if (!source || !target) return false;
    // חוקי ה-VIP: Root יכול הכל, או שה-UIDs זהים
    if (source->uid == ROOT_UID || source->uid == target->uid) {
        return true;
    }
    return false;
}

uint64_t sys_fork(void) {
    pcb_t* current = get_current_process();
    if (current && current->is_fork_child) {
        current->is_fork_child = 0;
        return 0;
    }
    return (uint64_t)fork_process();
}

uint64_t sys_exit(void) {
    scheduler_thread_exit();
    return 0; // לא יגיע לכאן לעולם
}

uint64_t sys_getpid(void) {
    pcb_t* proc = get_current_process();
    return proc ? (uint64_t)proc->pid : 0;
}

uint64_t sys_kill(uint64_t pid, int signo) {
    if (signo <= 0 || signo >= NSIG) return (uint64_t)-1;

    pcb_t* current = get_current_process();
    pcb_t* target = find_process_by_pid(pid);

    if (!target) return (uint64_t)-1;

    // אכיפת הרשאות Root/Owner
    if (!can_manage_process(current, target)) {
        uart_puts("[SECURITY] Permission denied for sys_kill\n");
        return (uint64_t)-1; // בשלב הבא תחליף ל- -EPERM
    }

    return (uint64_t)signal_send(target, signo);
}

uint64_t sys_signal(int signo, sig_handler_t handler) {
    pcb_t* proc = get_current_process();
    if (!proc || signo <= 0 || signo >= NSIG) return (uint64_t)-1;
    if (signo == SIGKILL || signo == SIGSTOP) return (uint64_t)-1;

    sig_handler_t old = proc->signals.handlers[signo];
    proc->signals.handlers[signo] = handler;
    return (uint64_t)old;
}

uint64_t sys_exec(const char* path, char* const argv[]) {
    return (uint64_t)exec_process(path, argv);
}

uint64_t sys_wait(int* status) {
    return (uint64_t)wait_process(status);
}

uint64_t sys_waitpid(int pid, int* status, int options) {
    return (uint64_t)waitpid_process(pid, status, options);
}