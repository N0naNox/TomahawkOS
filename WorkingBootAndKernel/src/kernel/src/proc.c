#include "proc.h"
#include "mm.h"
#include <stdlib.h>
#include <string.h>

static uint64_t next_pid = 1;
static uint64_t next_tid = 1;

static pcb_t* process_list = NULL;

pcb_t* create_process(const char* name, void (*entry)(void)) {
    pcb_t* p = malloc(sizeof(pcb_t));
    memset(p, 0, sizeof(pcb_t));

    p->pid = next_pid++;
    p->state = PROCESS_NEW;

    p->mm = mm_create();                     // your mm_struct builder
    p->main_thread = create_thread(p, entry);

    p->next = process_list;
    process_list = p;

    return p;
}

tcb_t* create_thread(pcb_t* proc, void (*entry)(void)) {
    tcb_t* t = malloc(sizeof(tcb_t));
    memset(t, 0, sizeof(tcb_t));

    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->parent = proc;

    t->kernel_stack = alloc_kernel_stack();  // you implement this

    // Prepare context
    t->context.rip = (uint64_t)entry;
    t->context.rsp = (uint64_t)t->kernel_stack;

    return t;
}
