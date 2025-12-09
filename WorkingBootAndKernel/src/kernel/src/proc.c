// proc.c
#include "proc.h"
#include "string.h"
#include <stdint.h>
#include "include/hal_port_io.h"   // for PIC EOI maybe used elsewhere
#include "include/uart.h"          // optional logging

/* Kernel stack size per thread */
#define KERNEL_STACK_SIZE (16 * 1024)

static uint64_t next_pid = 1;
static uint64_t next_tid = 1;

/* ready queue (simple singly-linked FIFO) */
static tcb_t* ready_head = NULL;
static tcb_t* ready_tail = NULL;

/* currently running thread */
tcb_t* current_thread = NULL;

/* process list (optional) */
static pcb_t* process_list = NULL;



/* simple kernel stack allocator */
void* alloc_kernel_stack(void) {
    void* s = malloc(KERNEL_STACK_SIZE);
    if (!s) return NULL;
    /* make stack top (stacks grow down). align to 16 */
    uintptr_t top = ((uintptr_t)s + KERNEL_STACK_SIZE) & ~0xFULL;
    return (void*)top;
}

/* create a thread (minimal) */
tcb_t* create_thread(pcb_t* proc, void (*entry)(void)) {
    tcb_t* t = malloc(sizeof(tcb_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(tcb_t));

    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->parent = proc;

    /* allocate kernel stack and set context */
    void* stack_top = alloc_kernel_stack();
    t->kernel_stack = stack_top;

    /* initialize CPU context */
    memset(&t->context, 0, sizeof(cpu_context_t));
    t->context.rip = (uint64_t)entry;

    /* When jumping to this thread, jmp will go to rip with the given rsp.
       So set rsp to stack_top and ensure there's a fake return address on stack
       (switch_context expects a saved RIP at [rsp]). We'll push a 0 as sentinel. */
    uint64_t *stack_ptr = (uint64_t*)stack_top;
    stack_ptr--;             // reserve one slot for return address
    *stack_ptr = 0;          // if function returns, it'll hit 0 -> crash, but kernel threads shouldn't return
    t->context.rsp = (uint64_t)stack_ptr;

    /* set other callee-saved regs to 0 */
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
    pcb_t* p = malloc(sizeof(pcb_t));
    if (!p) return NULL;
    memset(p, 0, sizeof(pcb_t));

    p->pid = next_pid++;
    p->state = PROCESS_NEW;
    p->mm = mm_create();    // assumes mm_create exists
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
    tcb_t* t = dequeue_ready();
    return t;
}

/* Schedule: save current context and switch to next runnable thread.
 * Called from timer IRQ handler or from kernel when we want to yield.
 */
void schedule(void) {
    tcb_t* prev = current_thread;
    tcb_t* next = pick_next();

    if (!next) {
        /* nothing to run: keep current running (or idle) */
        if (prev) {
            /* re-enqueue the running thread if still runnable */
            if (prev->state == THREAD_RUNNING) {
                prev->state = THREAD_READY;
                enqueue_ready(prev);
            }
        }
        return;
    }

    /* If no current thread, just start next */
    if (!prev) {
        next->state = THREAD_RUNNING;
        current_thread = next;
        /* switch_context with old == NULL is tricky, do a direct jump by
           calling switch_context with a dummy old on stack. We'll call a small
           assembly wrapper that treats NULL old as simply loading new. But
           easier: create a temporary cpu_context_t on stack and use switch_context. */
        cpu_context_t dummy_old;
        memset(&dummy_old, 0, sizeof(dummy_old));
        current_thread = next;
        next->state = THREAD_RUNNING;
        switch_context(&dummy_old, &next->context);
        return;
    }

    /* If scheduling to the same thread, just re-enqueue and return */
    if (prev == next) {
        /* put back and continue */
        enqueue_ready(next);
        return;
    }

    /* rotate: prev -> ready (if still runnable) */
    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        enqueue_ready(prev);
    }

    /* set states */
    next->state = THREAD_RUNNING;
    current_thread = next;

    /* perform context switch */
    switch_context(&prev->context, &next->context);

    /* when we return here, we are the resumed thread */
}

/* voluntary yield: put current to ready and schedule */
void thread_yield(void) {
    if (current_thread) {
        current_thread->state = THREAD_READY;
        enqueue_ready(current_thread);
        schedule();
    }
}
