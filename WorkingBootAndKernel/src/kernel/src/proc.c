/* proc.c - minimal process/thread management without libc malloc
 *
 * Uses a tiny bump allocator (kmalloc) for TCBs and stacks.
 * Replace kmalloc with your kernel allocator/page allocator later.
 */

#include "proc.h"
#include "string.h"          /* your kernel memcpy/memset/strlen */
#include "include/hal_port_io.h"
#include "include/uart.h"     /* optional logging */
#include "mm.h"
#include <stdint.h>

/* Kernel stack size per thread */
#define KERNEL_STACK_SIZE (16 * 1024)

/* tiny kmalloc region (1 MiB) - adjust as needed */
#define KMALLOC_REGION_SIZE (1024 * 1024)
static uint8_t kmalloc_region[KMALLOC_REGION_SIZE];
static uintptr_t kmalloc_ptr = (uintptr_t)kmalloc_region;
static uintptr_t kmalloc_end = (uintptr_t)kmalloc_region + KMALLOC_REGION_SIZE;

/* Simple kmalloc (no free). Not thread-safe. */
static void* kmalloc(size_t size, size_t align) {
    if (size == 0) return NULL;
    uintptr_t ptr = (kmalloc_ptr + (align - 1)) & ~(align - 1);
    if (ptr + size > kmalloc_end) {
        /* Out of memory */
        return NULL;
    }
    kmalloc_ptr = ptr + size;
    return (void*)ptr;
}

/* wrappers for convenience */
static void* zalloc(size_t size) {
    void* p = kmalloc(size, 16);
    if (p) memset(p, 0, size);
    return p;
}

/* local PID/TID generators */
static uint64_t next_pid = 1;
static uint64_t next_tid = 1;

/* ready queue (simple singly-linked FIFO) */
static tcb_t* ready_head = NULL;
static tcb_t* ready_tail = NULL;

/* currently running thread */
tcb_t* current_thread = NULL;

/* process list (optional) */
static pcb_t* process_list = NULL;

/* forward */
void enqueue_ready(tcb_t* t);
tcb_t* dequeue_ready(void);
tcb_t* pick_next(void);

/* allocate kernel stack: returns top-of-stack pointer (aligned to 16) */
void* alloc_kernel_stack(void) {
    void* stk = kmalloc(KERNEL_STACK_SIZE, 16);
    if (!stk) return NULL;
    uintptr_t top = ((uintptr_t)stk + KERNEL_STACK_SIZE) & ~0xFULL; /* align 16 */
    return (void*)top;
}

/* create a thread (minimal) */
tcb_t* create_thread(pcb_t* proc, void (*entry)(void)) {
    tcb_t* t = (tcb_t*) zalloc(sizeof(tcb_t));
    if (!t) return NULL;

    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->parent = proc;

    /* allocate kernel stack and set context */
    void* stack_top = alloc_kernel_stack();
    t->kernel_stack = stack_top;

    /* initialize CPU context */
    memset(&t->context, 0, sizeof(cpu_context_t));
    t->context.rip = (uint64_t)entry;

    /* prepare stack such that a return will try to pop 0 */
    uint64_t *stack_ptr = (uint64_t*)stack_top;
    stack_ptr--;             /* reserve one slot for return address */
    *stack_ptr = 0;          /* sentinel */
    t->context.rsp = (uint64_t)stack_ptr;

    /* callee-saved regs default zero */
    t->context.rbp = 0;
    t->context.rbx = 0;
    t->context.r12 = 0;
    t->context.r13 = 0;
    t->context.r14 = 0;
    t->context.r15 = 0;

    /* link into process thread list */
    if (proc) {
        t->next = proc->threads;
        proc->threads = t;
    }

    /* add to global ready queue */
    enqueue_ready(t);

    return t;
}

/* create new process (very small) */
pcb_t* create_process(const char* name, void (*entry)(void)) {
    (void)name; /* unused for now */

    pcb_t* p = (pcb_t*) zalloc(sizeof(pcb_t));
    if (!p) return NULL;
    p->pid = next_pid++;
    p->state = PROCESS_NEW;

    /* mm_create assumed to exist in your mm.c */
    p->mm = mm_create();

    /* create main thread */
    p->main_thread = create_thread(p, entry);
    p->threads = p->main_thread;

    /* link to process list */
    p->next = process_list;
    process_list = p;

    p->state = PROCESS_READY;
    return p;
}

/* ready queue helpers */
void enqueue_ready(tcb_t* t) {
    if (!t) return;
    t->next = NULL;
    if (!ready_tail) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

tcb_t* dequeue_ready(void) {
    tcb_t* t = ready_head;
    if (!t) return NULL;
    ready_head = t->next;
    if (!ready_head) ready_tail = NULL;
    t->next = NULL;
    return t;
}

/* pick next thread: simple round-robin */
tcb_t* pick_next(void) {
    return dequeue_ready();
}

/* extern assembler-provided switch_context */
extern void switch_context(cpu_context_t* old_ctx, cpu_context_t* new_ctx);

/* Schedule: save current context and switch to next runnable thread.
 * Called from timer IRQ handler or from kernel when we want to yield.
 */
void schedule(void) {
    tcb_t* prev = current_thread;
    tcb_t* next = pick_next();

    if (!next) {
        /* nothing to run: return (idle) */
        if (prev && prev->state == THREAD_RUNNING) {
            /* keep running */
            return;
        }
        return;
    }

    /* If no current thread, start next */
    if (!prev) {
        current_thread = next;
        next->state = THREAD_RUNNING;

        /* switch_context: we must provide an "old" context buffer to save into.
           Use a local temporary so later resume can return here (not used much). */
        cpu_context_t dummy_old;
        memset(&dummy_old, 0, sizeof(dummy_old));
        switch_context(&dummy_old, &next->context);
        return;
    }

    /* If switching to same thread, re-enqueue and return */
    if (prev == next) {
        enqueue_ready(next);
        return;
    }

    /* rotate prev -> ready if still running */
    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        enqueue_ready(prev);
    }

    next->state = THREAD_RUNNING;
    current_thread = next;

    /* perform context switch */
    switch_context(&prev->context, &next->context);

    /* resumed thread continues here */
}

/* voluntary yield */
void thread_yield(void) {
    if (current_thread) {
        current_thread->state = THREAD_READY;
        enqueue_ready(current_thread);
        schedule();
    }
}
