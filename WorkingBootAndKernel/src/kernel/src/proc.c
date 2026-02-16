#include "proc.h"
#include "string.h"
#include "mm.h"
#include "signal.h"
#include "paging.h"
#include <stdint.h>
#include "include/scheduler.h"
#include "include/elf_loader.h"
#include "include/vfs.h"
#include "include/frame_alloc.h"
#include <uart.h>

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

/* Global process list for signal delivery */
static pcb_t* process_list = NULL;

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
    t->context.rbp = t->context.rsp;

    /* Link to process thread list */
    if (proc) {
        t->next = proc->threads;
        proc->threads = t;
    }

    /* Add to scheduler ready queue */
    scheduler_add_thread(t);

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

    /* Initialize signal handling */
    signal_init(&p->signals);
    p->parent = NULL;
    p->exit_code = 0;

    /* Main thread */
    p->main_thread = create_thread(p, entry);
    p->threads = p->main_thread;
    p->next = NULL;

    /* Add to global process list */
    p->next = process_list;
    process_list = p;

    return p;
}

/* Get current process (from current thread) */
pcb_t* get_current_process(void) {
    tcb_t* current = scheduler_current();
    if (!current) return NULL;
    return current->parent;
}

/* Fork current process with COW */
int fork_process(void) {
    pcb_t* parent = get_current_process();
    if (!parent) {
        uart_puts("fork: no current process\n");
        return -1;
    }
    
    uart_puts("fork: cloning process ");
    uart_putu(parent->pid);
    uart_puts("\n");
    
    /* Create new PCB */
    pcb_t* child = (pcb_t*)zalloc(sizeof(pcb_t));
    if (!child) {
        uart_puts("fork: failed to allocate PCB\n");
        return -1;
    }
    
    child->pid = next_pid++;
    child->parent = parent;
    child->exit_code = 0;
    
    /* Copy signal handlers (but not pending signals) */
    memcpy(&child->signals, &parent->signals, sizeof(signal_struct_t));
    child->signals.pending = 0;  /* Child starts with no pending signals */
    
    /* Clone address space with COW */
    if (!parent->mm || !parent->mm->pml4_phys) {
        uart_puts("fork: parent has no address space\n");
        return -1;
    }
    
    child->mm = (mm_struct*)zalloc(sizeof(mm_struct));
    if (!child->mm) {
        uart_puts("fork: failed to allocate mm_struct\n");
        return -1;
    }
    
    child->mm->pml4_phys = paging_clone_cow(parent->mm->pml4_phys);
    if (!child->mm->pml4_phys) {
        uart_puts("fork: failed to clone page tables\n");
        return -1;
    }
    
    /* Clone main thread */
    tcb_t* parent_thread = scheduler_current();
    if (!parent_thread) {
        uart_puts("fork: no current thread\n");
        return -1;
    }
    
    tcb_t* child_thread = (tcb_t*)zalloc(sizeof(tcb_t));
    if (!child_thread) {
        uart_puts("fork: failed to allocate TCB\n");
        return -1;
    }
    
    child_thread->tid = next_tid++;
    child_thread->state = THREAD_READY;
    child_thread->parent = child;
    
    /* Copy context from parent */
    memcpy(&child_thread->context, &parent_thread->context, sizeof(cpu_context_t));
    
    /* Allocate kernel stack for child */
    child_thread->kernel_stack = alloc_kernel_stack();
    if (!child_thread->kernel_stack) {
        uart_puts("fork: failed to allocate kernel stack\n");
        return -1;
    }
    
    child->main_thread = child_thread;
    child->threads = child_thread;
    child->next = NULL;
    
    /* Mark this child so syscall handler can return 0 to it */
    child->is_fork_child = 1;
    
    /* Add child thread to scheduler */
    scheduler_add_thread(child_thread);
    
    /* Add child to process list */
    child->next = process_list;
    process_list = child;
    
    /* Add child to parent's children list */
    child->sibling_next = parent->children;
    parent->children = child;
    
    uart_puts("fork: created child process ");
    uart_putu(child->pid);
    uart_puts("\n");
    
    /* Return child PID to parent, 0 to child (will be set when child runs) */
    return (int)child->pid;
}

/* Find process by PID */
pcb_t* find_process_by_pid(uint64_t pid) {
    pcb_t* p = process_list;
    while (p) {
        if (p->pid == pid) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/* Replace current process image with new executable from file */
int exec_process(const char* path, char* const argv[]) {
    uart_puts("exec: loading ");
    uart_puts(path);
    uart_puts("\n");
    
    pcb_t* proc = get_current_process();
    if (!proc) {
        uart_puts("exec: no current process\n");
        return -1;
    }
    
    /* TODO: Open file from VFS - for now, return error if path is NULL */
    if (!path) {
        uart_puts("exec: NULL path\n");
        return -1;
    }
    
    /* For now, we'll assume the executable data is already in memory
     * In a full implementation, you would:
     * 1. Open the file using VFS
     * 2. Read the entire ELF into a buffer
     * 3. Load it using elf_load_executable_mm
     * 4. Free the buffer
     */
    
    /* Placeholder: In real implementation, load from VFS */
    uart_puts("exec: file loading not fully implemented - needs VFS integration\n");
    (void)argv;  /* Suppress unused warning */
    
    /* The actual exec would:
     * 1. Destroy current address space
     * 2. Create new address space
     * 3. Load ELF file
     * 4. Set up user stack with argc/argv
     * 5. Jump to entry point
     */
    
    return -1;  /* Not implemented yet */
}

/* Wait for child process to exit */
int wait_process(int* status) {
    return waitpid_process(-1, status, 0);
}

/* Wait for specific child process or any child */
int waitpid_process(int pid, int* status, int options) {
    pcb_t* parent = get_current_process();
    if (!parent) {
        uart_puts("wait: no current process\n");
        return -1;
    }
    
    uart_puts("wait: PID ");
    uart_putu(parent->pid);
    uart_puts(" waiting for child ");
    if (pid == -1) {
        uart_puts("(any)\n");
    } else {
        uart_putu(pid);
        uart_puts("\n");
    }
    
    /* Check if there are any children */
    if (!parent->children) {
        uart_puts("wait: no children\n");
        return -1;  /* ECHILD - no children */
    }
    
    /* Look for a zombie child matching the criteria */
    pcb_t* prev = NULL;
    pcb_t* child = parent->children;
    
    while (child) {
        /* Check if this child matches */
        int matches = (pid == -1) || (child->pid == (uint64_t)pid);
        
        if (matches && child->is_zombie) {
            uart_puts("wait: found zombie child PID ");
            uart_putu(child->pid);
            uart_puts(" with exit code ");
            uart_putu(child->exit_code);
            uart_puts("\n");
            
            /* Store exit status */
            if (status) {
                *status = child->exit_code;
            }
            
            uint64_t child_pid = child->pid;
            
            /* Remove child from parent's children list */
            if (prev) {
                prev->sibling_next = child->sibling_next;
            } else {
                parent->children = child->sibling_next;
            }
            
            /* TODO: Free child's resources (mm, threads, etc.) */
            /* For now, we just mark it as reaped */
            
            return (int)child_pid;
        }
        
        prev = child;
        child = child->sibling_next;
    }
    
    /* No zombie child found */
    (void)options;  /* TODO: handle WNOHANG option */
    
    /* In a real implementation, we would block the parent thread here
     * and wake it up when a child exits. For simplicity, we return -1. */
    uart_puts("wait: no zombie children (blocking not implemented)\n");
    
    /* Store current thread in wait queue (for future implementation) */
    parent->wait_queue = scheduler_current();
    
    /* TODO: Block current thread and yield to scheduler */
    /* For now, just return error */
    return -1;  /* EAGAIN or would block */
}
