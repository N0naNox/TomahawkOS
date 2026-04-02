#include "include/scheduler.h"
#include "include/proc.h"
#include "include/string.h"
#include "include/idt.h"
#include "include/signal.h"

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

static void enqueue_ready(tcb_t* t) {
    if (!t) return;
    t->next = NULL;
    if (!ready_head) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

void scheduler_init(void) {
    ready_head = ready_tail = NULL;
    current = NULL;
    memset(&kernel_context, 0, sizeof(kernel_context));
}

void scheduler_add_thread(tcb_t* t) {
    if (!t) return;
    t->state = THREAD_READY;
    enqueue_ready(t);
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

static void save_context_from_regs(tcb_t* t, regs_t* r) {
    if (!t || !r) return;
    t->context.rip = r->rip;
    t->context.rsp = r->rsp;
    t->context.rbp = r->rbp;
    t->context.rbx = r->rbx;
    t->context.r12 = r->r12;
    t->context.r13 = r->r13;
    t->context.r14 = r->r14;
    t->context.r15 = r->r15;
}

static void load_regs_from_context(regs_t* r, const cpu_context_t* c) {
    if (!r || !c) return;
    r->rip = c->rip;
    r->rsp = c->rsp;
    r->rbp = c->rbp;
    r->rbx = c->rbx;
    r->r12 = c->r12;
    r->r13 = c->r13;
    r->r14 = c->r14;
    r->r15 = c->r15;
}

void scheduler_tick(regs_t* r) {
    if (!r) return;

    /* If no current thread yet, start first ready thread */
    if (!current) {
        tcb_t* next = dequeue_next();
        if (!next) return;
        current = next;
        current->state = THREAD_RUNNING;
        load_regs_from_context(r, &next->context);
        return;
    }

    /* Save interrupted thread context */
    save_context_from_regs(current, r);

    tcb_t* next = dequeue_next();
    if (!next) {
        /* Check for pending signals before returning to same thread */
        if (current && current->parent) {
            signal_deliver(current->parent, r);
        }
        return;
    }

    /* Round-robin: place current back on ready queue if still runnable */
    if (current->state == THREAD_RUNNING || current->state == THREAD_READY) {
        current->state = THREAD_READY;
        enqueue_ready(current);
    }

    /* Switch to next */
    current = next;
    current->state = THREAD_RUNNING;
    load_regs_from_context(r, &next->context);
    
    /* Check for pending signals before returning to user mode */
    if (current && current->parent) {
        signal_deliver(current->parent, r);
    }
}

void scheduler_thread_exit(void) {
    /* Mark current thread as ZOMBIE */
    if (current) {
        current->state = THREAD_ZOMBIE;
        
        /* Mark parent process as zombie if this is the main thread */
        if (current->parent) {
            pcb_t* proc = current->parent;
            if (proc->main_thread == current) {
                proc->is_zombie = 1;

                /* Reparent children of the dying process to init / discard */
                process_reparent_children(proc);
                
                /* Wake up waiting parent if any */
                if (proc->parent && proc->parent->wait_queue) {
                    tcb_t* waiting_parent = proc->parent->wait_queue;
                    proc->parent->wait_queue = NULL;
                    
                    /* Add waiting parent back to ready queue */
                    if (waiting_parent->state == THREAD_BLOCKED) {
                        waiting_parent->state = THREAD_READY;
                        scheduler_add_thread(waiting_parent);
                    }
                }
            }
        }
    }
    
    /* Pick next thread */
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

void scheduler_block_current(void) {
    if (!current) return;

    /* Caller must have already set current->state = THREAD_BLOCKED */
    tcb_t* next = dequeue_next();
    if (!next) {
        /* Deadlock: nobody to run.  Just spin-wait for an IRQ to enqueue something. */
        current->state = THREAD_RUNNING;
        return;
    }

    next->state = THREAD_RUNNING;
    tcb_t* old = current;
    current = next;
    context_switch(&old->context, &next->context);
    /* When we return here, the parent was woken up again */
}
