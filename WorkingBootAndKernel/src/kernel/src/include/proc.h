#ifndef PROC_H
#define PROC_H

#include "mm.h"
#include "signal.h"
#include <stdint.h>

/* Thread states */
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_ZOMBIE
} thread_state_t;

/* CPU context layout (still needed for thread creation) */
typedef struct cpu_context {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} cpu_context_t;

typedef struct tcb {
    uint64_t tid;
    thread_state_t state;
    cpu_context_t context;

    struct pcb* parent;
    void* kernel_stack;
    struct tcb* next;
} tcb_t;

typedef struct pcb {
    uint64_t pid;
    mm_struct* mm;              /* address space info */
    tcb_t* main_thread;
    tcb_t* threads;
    struct pcb* next;
    
    /* Signal handling */
    signal_struct_t signals;    /* signal handlers, pending, blocked */
    struct pcb* parent;         /* parent process (for SIGCHLD) */
    int exit_code;              /* exit status */
} pcb_t;

/* API */
pcb_t* create_process(const char* name, void (*entry)(void));
tcb_t* create_thread(pcb_t* proc, void (*entry)(void));

/* allocate kernel stack */
void* alloc_kernel_stack(void);

#endif /* PROC_H */
