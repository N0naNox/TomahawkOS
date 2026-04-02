#pragma once

#include "proc.h"
#include "scheduler.h"
#include "signal.h"
#include "uart.h"

#define ROOT_UID 0

uint64_t sys_kill(uint64_t target_pid, int signo);
static bool can_manage_process(pcb_t* subject, pcb_t* object);
uint64_t sys_fork(void);
uint64_t sys_exit(void);
uint64_t sys_getpid(void);
uint64_t sys_kill(uint64_t pid, int signo);
uint64_t sys_signal(int signo, sig_handler_t handler);
uint64_t sys_exec(const char* path, char* const argv[]);
uint64_t sys_wait(int* status);
uint64_t sys_waitpid(int pid, int* status, int options);

/* Job control syscalls */
uint64_t sys_setpgid(uint64_t pid, uint64_t pgid);
uint64_t sys_getpgid(uint64_t pid);
uint64_t sys_setsid(void);
uint64_t sys_tcsetpgrp(uint64_t pgid);
uint64_t sys_tcgetpgrp(void);

