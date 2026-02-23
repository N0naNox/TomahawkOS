#ifndef PROC_H
#define PROC_H

#include "mm.h"
#include "signal.h"
#include <stdint.h>

#define MAX_FILES 32   

/* waitpid option flags */
#define WNOHANG 1   /* Return immediately if no zombie child */

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

    uint32_t uid;
    uint32_t gid;
    

    mm_struct* mm;              /* address space info */
    tcb_t* main_thread;
    tcb_t* threads;
    struct pcb* next;

    struct file* fd_table[MAX_FILES];  /* file descriptors */

    
    /* Signal handling */
    signal_struct_t signals;    /* signal handlers, pending, blocked */
    struct pcb* parent;         /* parent process (for SIGCHLD) */
    int exit_code;              /* exit status */
    int is_fork_child;          /* 1 if this is a newly forked child */
    
    /* Process state for wait() */
    int is_zombie;              /* 1 if process has exited but not reaped */
    struct pcb* children;       /* list of child processes */
    struct pcb* sibling_next;   /* next sibling in parent's children list */
    tcb_t* wait_queue;          /* thread waiting for this process (parent) */
} pcb_t;

/* API */
pcb_t* create_process(const char* name, void (*entry)(void));
tcb_t* create_thread(pcb_t* proc, void (*entry)(void));

/* Fork current process with COW (returns child PID or 0 in child, -1 on error) */
int fork_process(void);

/* Replace current process image with new executable from file */
int exec_process(const char* path, char* const argv[]);

/* Wait for child process to exit (returns child PID or -1, stores exit status) */
int wait_process(int* status);

/* Wait for specific child process (pid > 0) or any child (pid = -1) */
int waitpid_process(int pid, int* status, int options);

/* Get current process */
pcb_t* get_current_process(void);

/* Find process by PID */
pcb_t* find_process_by_pid(uint64_t pid);

/* allocate kernel stack */
void* alloc_kernel_stack(void);

/* Reap a zombie child: free its resources and remove from process list */
void process_reap(pcb_t* child);

/* Reparent all children of 'dying' to init (PID 1) or discard zombies */
void process_reparent_children(pcb_t* dying);

#endif /* PROC_H */
