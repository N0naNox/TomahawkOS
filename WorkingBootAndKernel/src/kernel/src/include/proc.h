#ifndef PROC_H
#define PROC_H


#include "mm.h"


typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_ZOMBIE
} thread_state_t;

typedef struct cpu_context {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;

    /* Optional extra registers */
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

    struct pcb* parent;       // Pointer back to parent process

    void* kernel_stack;       // Top of kernel stack for this thread

    struct tcb* next;        
} tcb_t;

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state_t;


typedef struct pcb {
    uint64_t pid;
    process_state_t state;

    mm_struct* mm;          

    tcb_t* main_thread;       
    tcb_t* threads;

    struct pcb* next;         
} pcb_t;


pcb_t* create_process(const char* name, void (*entry)(void));
tcb_t* create_thread(pcb_t* proc, void (*entry)(void));


void schedule(void);
#endif