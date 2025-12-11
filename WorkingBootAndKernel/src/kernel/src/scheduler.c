#include "include/scheduler.h"
#include "include/proc.h"
#include "include/string.h"

/* Basic cooperative round-robin scheduler.
 * - Single global ready queue (singly linked list using t->next)
 * - scheduler_yield() will perform a context switch to the next ready thread
 * - Uses assembly context_switch(old_ctx, new_ctx) implemented in switch.S
 */

/* External context switch implemented in assembly. 
 * Arguments: rdi = pointer to old cpu_context_t, rsi = pointer to new cpu_context_t
 */
extern void context_switch(void* old_ctx, void* new_ctx);

/* Ready queue head/tail */
static tcb_t* ready_head = NULL;
static tcb_t* ready_tail = NULL;

/* Current running thread */
static tcb_t* current = NULL;

/* Kernel context placeholder used when switching from kernel to first thread */
static cpu_context_t kernel_context;

void scheduler_init(void) {
    ready_head = ready_tail = NULL;
    current = NULL;
    memset(&kernel_context, 0, sizeof(kernel_context));
}

void scheduler_add_thread(tcb_t* t) {
    if (!t) return;
    t->state = THREAD_READY;
    t->next = NULL;
    if (!ready_head) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

/* Pop next ready thread or return NULL */
static tcb_t* dequeue_next(void) {
    tcb_t* t = ready_head;
    if (!t) return NULL;
    ready_head = t->next;
    if (!ready_head) ready_tail = NULL;
    t->next = NULL;
    return t;
}

tcb_t* scheduler_current(void) {
    return current;
}

/* The core scheduler: cooperative. Saves current context and switches. */
void scheduler_yield(void) {
    tcb_t* old = current;
    tcb_t* next = dequeue_next();

    if (!next) {
        /* No other ready thread; continue running current */
        return;
    }

    if (old) {
        /* If current was running, put it back on ready queue */
        if (old->state == THREAD_RUNNING) {
            old->state = THREAD_READY;
            scheduler_add_thread(old);
        }
    }

    /* Set up next */
    next->state = THREAD_RUNNING;
    current = next;

    if (!old) {
        /* No previous thread (we are in kernel); switch from kernel_context */
        context_switch(&kernel_context, &next->context);
    } else {
        context_switch(&old->context, &next->context);
    }
}

void scheduler_thread_exit(void) {
    /* Mark current as ZOMBIE and pick next */
    if (current) current->state = THREAD_ZOMBIE;
    tcb_t* next = dequeue_next();
    if (!next) {
        /* No ready thread: hang */
        for(;;) { __asm__ volatile("hlt"); }
    }
    next->state = THREAD_RUNNING;
    tcb_t* old = current;
    current = next;
    if (old) {
        context_switch(&old->context, &next->context);
    } else {
        context_switch(&kernel_context, &next->context);
    }
}
