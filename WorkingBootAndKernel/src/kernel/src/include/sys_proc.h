#pragma once

#include "proc.h"
#include "scheduler.h"
#include "signal.h"
#include "uart.h"

uint64_t sys_kill(uint64_t target_pid, int signo);
static bool can_manage_process(pcb_t* subject, pcb_t* object);
uint64_t sys_fork(void);
uint64_t sys_exit(void);
uint64_t sys_getpid(void);
uint64_t sys_kill(uint64_t pid, int signo);
uint64_t sys_signal(int signo, sig_handler_t handler);

