#include "proc.h"
#include "string.h"
#include "mm.h"
#include <stdint.h>

/* Kernel stack size per thread */
#define KERNEL_STACK_SIZE (16 * 1024)

/* Tiny bump allocator for TCBs and stacks */
#define KMALLOC_REGION_SIZE (1024 * 1024)
static uint8_t kmalloc_region[KMALLOC_REGION_SIZE];
static uintptr_t kmalloc_ptr = (uintptr_t)kmalloc_region;
static uintptr_t kmalloc_end = (uintptr_t)kmalloc_region + KMALLOC_REGION_SIZE;

static void* kmalloc(size_t size, size_t align) {
    if (size == 0) return NULL;
    uintptr_t ptr = (kmalloc_ptr + (align - 1)) & ~(align - 1);
    if (ptr + size > kmalloc_end) return NULL;
    kmalloc_ptr = ptr + size;
    return (void*)ptr;
}

static void* zalloc(size_t size) {
    void* p = kmalloc(size, 16);
    if (p) memset(p, 0, size);
    return p;
}

static uint64_t next_pid = 1;
static uint64_t next_tid = 1;

/* Allocate kernel stack (top pointer) */
void* alloc_kernel_stack(void) {
    void* stk = kmalloc(KERNEL_STACK_SIZE, 16);
    if (!stk) return NULL;
    uintptr_t top = ((uintptr_t)stk + KERNEL_STACK_SIZE) & ~0xFULL;
    return (void*)top;
}

/* Create a thread (minimal, no scheduling) */
tcb_t* create_thread(pcb_t* proc, void (*entry)(void)) {
    tcb_t* t = (tcb_t*) zalloc(sizeof(tcb_t));
    if (!t) return NULL;

    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->parent = proc;

    t->kernel_stack = alloc_kernel_stack();

    /* Initialize context */
    memset(&t->context, 0, sizeof(cpu_context_t));
    t->context.rip = (uint64_t)entry;

    uint64_t* stack_ptr = (uint64_t*)t->kernel_stack;
    stack_ptr--;
    *stack_ptr = 0;
    t->context.rsp = (uint64_t)stack_ptr;

    /* Link to process thread list */
    if (proc) {
        t->next = proc->threads;
        proc->threads = t;
    }

    return t;
}

/* Create a process (minimal) */
pcb_t* create_process(const char* name, void (*entry)(void)) {
    (void)name;

    pcb_t* p = (pcb_t*) zalloc(sizeof(pcb_t));
    if (!p) return NULL;
    p->pid = next_pid++;

    /* Address space */
    p->mm = mm_create();

    /* Main thread */
    p->main_thread = create_thread(p, entry);
    p->threads = p->main_thread;
    p->next = NULL;

    return p;
}
