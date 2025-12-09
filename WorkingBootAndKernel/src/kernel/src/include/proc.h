#ifndef PROC_H
#define PROC_H

#include "mm.h"
#include <stdint.h>

/* Thread states */
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_ZOMBIE
} thread_state_t;

/* CPU context layout used by switch_context() (order/size matters) */
typedef struct cpu_context {
    uint64_t rip;   /* 0 */
    uint64_t rsp;   /* 8 */
    uint64_t rbp;   /* 16 */

    /* callee-saved registers */
    uint64_t rbx;   /* 24 */
    uint64_t r12;   /* 32 */
    uint64_t r13;   /* 40 */
    uint64_t r14;   /* 48 */
    uint64_t r15;   /* 56 */
} cpu_context_t;

typedef struct tcb {
    uint64_t tid;
    thread_state_t state;
    cpu_context_t context;

    struct pcb* parent;       /* back pointer */

    void* kernel_stack;       /* top of kernel stack (stack grows down) */

    struct tcb* next;         /* linked list for queues */
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

    mm_struct* mm;          /* address space info */

    tcb_t* main_thread;     /* main thread */
    tcb_t* threads;         /* linked list of threads */

    struct pcb* next;
} pcb_t;

/* API */
pcb_t* create_process(const char* name, void (*entry)(void));
tcb_t* create_thread(pcb_t* proc, void (*entry)(void));

void enqueue_ready(tcb_t* t);
tcb_t* dequeue_ready(void);
tcb_t* pick_next(void);

void thread_yield(void);
void schedule(void);

/* low-level: stack allocator and context switch wrapper */
void* alloc_kernel_stack(void);

/* switch_context(old_ctx, new_ctx)
 *   - saves callee-saved registers+RIP+RSP into *old_ctx
 *   - restores regs from *new_ctx and jumps to new_ctx->rip
 */
void switch_context(cpu_context_t* old_ctx, cpu_context_t* new_ctx);

#endif /* PROC_H */
