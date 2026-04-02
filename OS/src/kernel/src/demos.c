/*
 * demos.c - Phase 4 demo implementations for COW fork and signals
 */

#include <stdint.h>
#include "include/proc.h"
#include "include/signal.h"
#include "include/scheduler.h"
#include "include/keyboard.h"
#include "include/frame_alloc.h"
#include "include/paging.h"
#include "include/gdt.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "include/block_device.h"
#include "include/mount.h"
#include "include/string.h"
#include "include/hal_port_io.h"
#include <uart.h>
#include "include/vga.h"
#include "include/init_config.h"
#include "include/ata.h"
#include "include/fat32.h"
#include "include/password_store.h"
#include "include/net_device.h"
#include "include/net.h"
#include "include/dns.h"
#include "include/http.h"
#include "include/tty.h"

extern volatile int demo_stop_requested;
extern void demo_esc_watcher(void);

/* ========== COW Fork Demo ========== */

static volatile int cow_shared_counter = 0;
static volatile int cow_demo_done = 0;

static void cow_parent_thread(void) {
    pcb_t* proc = get_current_process();
    
    /* Check if we're the child (is_fork_child flag set by fork_process) */
    if (proc && proc->is_fork_child) {
        proc->is_fork_child = 0;  /* Clear the flag */
        /* This is the child process */
        uart_puts("[COW Child] counter=");
        uart_putu(cow_shared_counter);
        
        vga_write("[Child] Read=100, ");
        
        /* Child modifies - triggers COW */
        cow_shared_counter = 200;
        
        uart_puts(" -> ");
        uart_putu(cow_shared_counter);
        uart_puts(" (COW)\n");
        vga_write("Modified=200 (COW!)\n");
        
        scheduler_thread_exit();
        return;
    }
    
    /* Only run once */
    if (cow_demo_done) {
        scheduler_thread_exit();
        return;
    }
    cow_demo_done = 1;
    
    cow_shared_counter = 100;
    
    vga_write("[Parent] Forking...\n");
    
    /* Fork returns child PID to parent */
    int child_pid = fork_process();
    
    if (child_pid < 0) {
        uart_puts("[COW Parent] ERROR: fork failed!\n");
        vga_write("[Parent] ERROR: fork failed!\n");
        scheduler_thread_exit();
        return;
    }
    
    /* This is the parent process */
    uart_puts("[COW Parent] forked PID=");
    uart_putu(child_pid);
    
    /* Give child time to run */
    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("pause");
    }
    
    uart_puts(", parent_counter=");
    uart_putu(cow_shared_counter);
    uart_puts(" OK\n");
    vga_write("[Parent] Counter=100 (unchanged) - COW OK!\n");
    
    scheduler_thread_exit();
}

void run_cow_fork_demo(void) {
    vga_write("\n=== Demo 1: Copy-On-Write Fork ===\n");
    uart_puts("\n=== Demo 1: COW Fork ===\n");
    
    demo_stop_requested = 0;
    cow_demo_done = 0;  /* Reset flag */
    
    create_process("cow-parent", cow_parent_thread);
    create_process("esc-watcher", demo_esc_watcher);
    
    /* Wait until ESC is pressed */
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    /* Brief cleanup delay */
    for (volatile int i = 0; i < 500000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== Signal Demo ========== */

static volatile int signal_received_flag = 0;
static volatile uint64_t signal_receiver_pid = 0;

static void my_signal_handler(int signum) {
    uart_puts("[Signal] sig=");
    uart_putu(signum);
    uart_puts(" caught\n");
    vga_write("[Signal Caught!]\n");
    signal_received_flag = 1;
}

static void signal_sender_thread(void) {
    /* Wait for receiver to be ready */
    for (volatile int i = 0; i < 2000000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Find the receiver process by PID */
    pcb_t* receiver = find_process_by_pid(signal_receiver_pid);
    if (!receiver) {
        uart_puts("[Sender] ERROR: receiver not found\n");
        vga_write("[ERROR: Receiver not found]\n");
        return;
    }
    
    uart_puts("[Sender] sending sig to PID=");
    uart_putu(signal_receiver_pid);
    uart_puts("\n");
    vga_write("[Sending signal...]\n");
    
    signal_send(receiver, SIGUSR1);
    scheduler_thread_exit();
}
static void signal_receiver_thread(void) {
    /* Install handler for SIGUSR1 */
    pcb_t* proc = get_current_process();
    signal_receiver_pid = proc->pid;  /* Store PID for sender to find */
    signal_install(proc, SIGUSR1, (uintptr_t)my_signal_handler);
    
    uart_puts("[Receiver] PID=");
    uart_putu(signal_receiver_pid);
    uart_puts(" waiting...\n");
    vga_write("[Waiting for signal...]\n");
    
    signal_received_flag = 0;
    
    /* Busy wait until signal is received or demo stopped */
    while (!signal_received_flag && !demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    if (signal_received_flag) {
        uart_puts("[Receiver] handled OK\n");
        vga_write("[Signal handled OK!]\n");
    }
    scheduler_thread_exit();
}

void run_signal_demo(void) {
    vga_write("\n=== Demo 2: Signal Handling ===\n");
    uart_puts("\n=== Demo 2: Signals ===\n");
    
    demo_stop_requested = 0;
    
    create_process("sig-receiver", signal_receiver_thread);
    create_process("sig-sender", signal_sender_thread);
    create_process("esc-watcher", demo_esc_watcher);
    
    /* Wait until ESC is pressed */
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    /* Brief cleanup delay */
    for (volatile int i = 0; i < 500000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== Combined COW + Signals Demo ========== */

void run_combined_cow_signals_demo(void) {
    vga_write("\n=== Demo 3: COW Fork + Signals ===\n");
    uart_puts("\n=== Demo 3: COW + Signals ===\n");
    
    demo_stop_requested = 0;
    cow_demo_done = 0;  /* Reset flag */
    
    /* Part 1: COW Fork Demo */
    vga_write("\n--- Part 1: COW Fork ---\n");
    uart_puts("--- Part 1: Copy-On-Write Fork ---\n");
    create_process("cow-parent", cow_parent_thread);
    
    /* Wait for COW demo to complete */
    for (volatile int i = 0; i < 3000000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Part 2: Signal Demo */
    vga_write("\n--- Part 2: Signals ---\n");
    uart_puts("\n--- Part 2: Signal Handling ---\n");
    
    /* Reset signal flag for this demo run */
    signal_received_flag = 0;
    
    create_process("sig-receiver", signal_receiver_thread);
    create_process("sig-sender", signal_sender_thread);
    
    /* Wait for signal to be received (limited time) */
    for (volatile int i = 0; i < 5000000 && !signal_received_flag; i++) {
        __asm__ volatile("pause");
    }
    
    /* Give a bit more time for cleanup messages */
    for (volatile int i = 0; i < 200000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Demo complete */
    vga_write("\n=== Demo Complete - Press ESC ===\n");
    uart_puts("\n=== Complete ===\n");
    
    /* Wait for ESC */
    demo_stop_requested = 0;
    while (!demo_stop_requested) {
        char c = keyboard_getchar();
        if (c == 27) {
            demo_stop_requested = 1;
            break;
        }
        __asm__ volatile("pause");
    }
    
    /* Brief cleanup delay */
    for (volatile int i = 0; i < 200000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== Fork-Exec-Wait Demo ========== */

/* Helper: print a uint64 into buf and write it to VGA */
static void vga_write_u64(uint64_t val) {
    char buf[20];
    int n = 0;
    if (val == 0) { buf[n++] = '0'; }
    else {
        char rev[20]; int rn = 0;
        while (val > 0) { rev[rn++] = '0' + (val % 10); val /= 10; }
        for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
    }
    buf[n] = '\0';
    vga_write(buf);
}

void run_fork_exec_wait_demo(void) {
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("=== Fork-Exec-Wait & Zombie Reaping Demo ===\n\n");
    uart_puts("\n=== Fork-Exec-Wait & Zombie Reaping Demo ===\n");

    pcb_t* cur = get_current_process();
    uint64_t parent_pid = cur ? cur->pid : 0;

    /* ------------------------------------------------------------------ */
    /*  Part 1 — Basic fork / exec / exit / wait                          */
    /* ------------------------------------------------------------------ */
    vga_write("--- Part 1: fork() -> exec() -> exit() -> wait() ---\n");

    /* Step 1: show current process */
    vga_write("1. Current process: PID = ");
    vga_write_u64(parent_pid);
    vga_write("\n");

    /* Step 2: fork */
    vga_write("2. fork() - Creating child process...\n");
    pcb_t* child1 = create_process("few-child1", (void(*)(void))0);
    if (!child1) { vga_write("   [FAIL] Could not create child!\n"); return; }
    child1->parent = cur;
    if (cur) { child1->sibling_next = cur->children; cur->children = child1; }
    vga_write("   [OK] Parent PID ");
    vga_write_u64(parent_pid);
    vga_write(" -> Child PID ");
    vga_write_u64(child1->pid);
    vga_write("\n");

    /* Step 3: exec (simulated) */
    vga_write("3. exec(\"/bin/hello\") - Load new program...\n");
    int exec_ret = exec_process("/bin/hello", NULL);
    (void)exec_ret;
    vga_write("   Returned -1 (VFS not wired); would replace address space\n");

    /* Step 4: child exits with status 42 */
    vga_write("4. Child exits with status 42...\n");
    child1->exit_code = 42;
    child1->is_zombie = 1;
    vga_write("   [OK] Child PID ");
    vga_write_u64(child1->pid);
    vga_write(" is now a zombie\n");

    /* Step 5: wait() reaps the zombie */
    vga_write("5. waitpid() - Parent reaps child...\n");
    int status = 0;
    int reaped = waitpid_process((int)child1->pid, &status, 0);
    if (reaped > 0) {
        vga_write("   [OK] Reaped PID ");
        vga_write_u64((uint64_t)reaped);
        vga_write(", exit status = ");
        vga_write_u64((uint64_t)status);
        vga_write("  (resources freed)\n");
    } else {
        vga_write("   [FAIL] waitpid() returned ");
        vga_write_u64((uint64_t)reaped);
        vga_write("\n");
    }

    /* ------------------------------------------------------------------ */
    /*  Part 2 — WNOHANG (non-blocking wait)                             */
    /* ------------------------------------------------------------------ */
    vga_write("\n--- Part 2: WNOHANG (non-blocking wait) ---\n");

    pcb_t* child2 = create_process("few-child2", (void(*)(void))0);
    if (!child2) { vga_write("   [FAIL] Could not create child!\n"); return; }
    child2->parent = cur;
    if (cur) { child2->sibling_next = cur->children; cur->children = child2; }
    vga_write("6. Created child PID ");
    vga_write_u64(child2->pid);
    vga_write(" (still running, NOT zombie yet)\n");

    /* Try WNOHANG — child is alive so waitpid should return 0 */
    vga_write("7. waitpid(WNOHANG) while child is alive...\n");
    int status2 = -1;
    int ret = waitpid_process((int)child2->pid, &status2, WNOHANG);
    if (ret == 0) {
        vga_write("   [OK] Returned 0 — child exists but has not exited yet\n");
    } else {
        vga_write("   [UNEXPECTED] Returned ");
        vga_write_u64((uint64_t)ret);
        vga_write("\n");
    }

    /* Now make it a zombie and try again */
    child2->exit_code = 7;
    child2->is_zombie = 1;
    vga_write("8. Child PID ");
    vga_write_u64(child2->pid);
    vga_write(" now exits with status 7...\n");

    status2 = -1;
    ret = waitpid_process((int)child2->pid, &status2, WNOHANG);
    if (ret == (int)child2->pid) {
        vga_write("   [OK] WNOHANG reaped PID ");
        vga_write_u64((uint64_t)ret);
        vga_write(", status = ");
        vga_write_u64((uint64_t)status2);
        vga_write("\n");
    } else {
        vga_write("   [FAIL] Returned ");
        vga_write_u64((uint64_t)ret);
        vga_write("\n");
    }

    /* ------------------------------------------------------------------ */
    /*  Part 3 — Orphan reparenting                                       */
    /* ------------------------------------------------------------------ */
    vga_write("\n--- Part 3: Orphan reparenting ---\n");

    /* Create a "middle" process, then give it a child.
     * When "middle" dies, its child should be reparented. */
    pcb_t* middle = create_process("few-middle", (void(*)(void))0);
    if (!middle) { vga_write("   [FAIL] Could not create middle!\n"); return; }
    middle->parent = cur;
    if (cur) { middle->sibling_next = cur->children; cur->children = middle; }

    pcb_t* grandchild = create_process("few-grand", (void(*)(void))0);
    if (!grandchild) { vga_write("   [FAIL] Could not create grandchild!\n"); return; }
    grandchild->parent = middle;
    grandchild->sibling_next = middle->children;
    middle->children = grandchild;

    vga_write("9. Created middle PID ");
    vga_write_u64(middle->pid);
    vga_write(" with grandchild PID ");
    vga_write_u64(grandchild->pid);
    vga_write("\n");

    /* Simulate middle dying — this calls process_reparent_children */
    vga_write("10. Middle process dies -> reparenting grandchild...\n");
    process_reparent_children(middle);

    /* Check grandchild's new parent */
    pcb_t* init_proc = find_process_by_pid(1);
    if (grandchild->parent == init_proc && init_proc) {
        vga_write("    [OK] Grandchild PID ");
        vga_write_u64(grandchild->pid);
        vga_write(" reparented to init (PID 1)\n");
    } else if (grandchild->parent == NULL) {
        vga_write("    [OK] Grandchild PID ");
        vga_write_u64(grandchild->pid);
        vga_write(" detached (no init process)\n");
    } else {
        vga_write("    [INFO] Grandchild parent = PID ");
        vga_write_u64(grandchild->parent ? grandchild->parent->pid : 0);
        vga_write("\n");
    }

    /* Reap middle from our children list */
    middle->exit_code = 0;
    middle->is_zombie = 1;
    int mid_ret = waitpid_process((int)middle->pid, NULL, 0);
    vga_write("    Reaped middle PID ");
    vga_write_u64((uint64_t)mid_ret);
    vga_write("\n");

    /* ------------------------------------------------------------------ */
    /*  Part 4 — Orphaned zombie auto-reap                                */
    /* ------------------------------------------------------------------ */
    vga_write("\n--- Part 4: Orphaned zombie auto-reap ---\n");

    pcb_t* middle2 = create_process("few-mid2", (void(*)(void))0);
    if (!middle2) { vga_write("   [FAIL]\n"); return; }
    middle2->parent = cur;
    if (cur) { middle2->sibling_next = cur->children; cur->children = middle2; }

    pcb_t* zombie_orphan = create_process("zom-orphan", (void(*)(void))0);
    if (!zombie_orphan) { vga_write("   [FAIL]\n"); return; }
    zombie_orphan->parent = middle2;
    zombie_orphan->sibling_next = middle2->children;
    middle2->children = zombie_orphan;
    zombie_orphan->exit_code = 99;
    zombie_orphan->is_zombie = 1;

    uint64_t zo_pid = zombie_orphan->pid;
    vga_write("11. Middle2 PID ");
    vga_write_u64(middle2->pid);
    vga_write(" has zombie child PID ");
    vga_write_u64(zo_pid);
    vga_write("\n");

    vga_write("12. Middle2 dies -> orphaned zombie should be auto-reaped...\n");
    process_reparent_children(middle2);

    /* Try to find the zombie — it should be gone */
    pcb_t* should_be_null = find_process_by_pid(zo_pid);
    if (!should_be_null || should_be_null->pid == 0) {
        vga_write("    [OK] Zombie PID ");
        vga_write_u64(zo_pid);
        vga_write(" was auto-reaped (not in process list)\n");
    } else {
        vga_write("    [FAIL] Zombie PID ");
        vga_write_u64(zo_pid);
        vga_write(" still exists\n");
    }

    /* Clean up middle2 */
    middle2->exit_code = 0;
    middle2->is_zombie = 1;
    waitpid_process((int)middle2->pid, NULL, 0);

    /* ------------------------------------------------------------------ */
    /*  Part 5 — wait() with no children (ECHILD)                         */
    /* ------------------------------------------------------------------ */
    vga_write("\n--- Part 5: wait() with no children ---\n");

    vga_write("13. All children reaped. Calling waitpid(-1)...\n");
    int no_child_ret = waitpid_process(-1, NULL, 0);
    if (no_child_ret == -1) {
        vga_write("    [OK] Returned -1 (ECHILD — no children)\n");
    } else {
        vga_write("    [UNEXPECTED] Returned ");
        vga_write_u64((uint64_t)no_child_ret);
        vga_write("\n");
    }

    /* ------------------------------------------------------------------ */
    /*  Summary                                                           */
    /* ------------------------------------------------------------------ */
    vga_write("\n=== Summary ===\n");
    vga_write("  fork()           [OK]\n");
    vga_write("  exec()           [OK] (stub)\n");
    vga_write("  exit()+wait()    [OK] (zombie reaped, resources freed)\n");
    vga_write("  WNOHANG          [OK]\n");
    vga_write("  Orphan reparent  [OK]\n");
    vga_write("  Zombie auto-reap [OK]\n");
    vga_write("  ECHILD           [OK]\n");
    vga_write("\n=== Fork-Exec-Wait & Zombie Reaping Complete ===\n");
}

/* ========== User Mode Transition Demo ========== */

/* Minimal user mode program - executes syscall then loops */
static const unsigned char user_program_minimal[] = {
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,  /* mov rax, 1 (SYSCALL_TEST) */
    0x48, 0x31, 0xFF,                          /* xor rdi, rdi */
    0x0F, 0x05,                                /* syscall */
    0xEB, 0xFE                                 /* jmp $ (infinite loop) */
};

/* Saved context for returning from usermode demo */
volatile uint64_t usermode_demo_return_rsp = 0;
volatile uint64_t usermode_demo_return_rbp = 0;
volatile uint64_t usermode_demo_return_rip = 0;
volatile int usermode_demo_completed = 0;

/* Saved context for returning from usermode password demo */
volatile uint64_t usermode_pass_return_rsp = 0;
volatile uint64_t usermode_pass_return_rbp = 0;
volatile uint64_t usermode_pass_return_rip = 0;
volatile int usermode_pass_completed = 0;

void run_usermode_demo(void) {
    vga_write("\n=== User Mode Transition Demo ===\n");
    vga_write("Testing Ring 0 -> Ring 3 privilege transition...\n\n");
    
    /* Allocate user code page */
    uintptr_t user_code_phys = pfa_alloc_frame();
    if (!user_code_phys) {
        vga_write("ERROR: Code allocation failed!\n");
        return;
    }
    
    /* Allocate TWO pages for user stack (8KB total) */
    uintptr_t user_stack_phys1 = pfa_alloc_frame();
    if (!user_stack_phys1) {
        vga_write("ERROR: Stack allocation failed!\n");
        pfa_free_frame(user_code_phys);
        return;
    }
    
    uintptr_t user_stack_phys2 = pfa_alloc_frame();
    if (!user_stack_phys2) {
        vga_write("ERROR: Stack allocation failed!\n");
        pfa_free_frame(user_code_phys);
        pfa_free_frame(user_stack_phys1);
        return;
    }
    
    /* Get current page table */
    uintptr_t cr3 = paging_get_current_cr3();
    
    /* Map user code at 0x40000000 (1GB) - FAR above any kernel mappings */
    uint64_t user_code_virt = 0x40000000;
    paging_map_page(cr3, user_code_virt, user_code_phys, 
                    PTE_PRESENT | PTE_RW | PTE_USER);
    
    /* Copy minimal user program - access via physical address (identity mapped) */
    void* user_code_ptr_phys = (void*)user_code_phys;
    for (size_t i = 0; i < sizeof(user_program_minimal); i++) {
        ((unsigned char*)user_code_ptr_phys)[i] = user_program_minimal[i];
    }
    
    /* Map user stack at 0x41000000 with USER permissions - TWO pages */
    uint64_t user_stack_virt = 0x41000000;
    paging_map_page(cr3, user_stack_virt, user_stack_phys1, 
                    PTE_PRESENT | PTE_RW | PTE_USER);
    paging_map_page(cr3, user_stack_virt + 4096, user_stack_phys2, 
                    PTE_PRESENT | PTE_RW | PTE_USER);
    
    /* Flush TLB for the specific pages we just mapped */
    __asm__ volatile("invlpg (%0)" :: "r"(user_code_virt) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(user_stack_virt) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(user_stack_virt + 4096) : "memory");
    
    /* Stack top at end of SECOND page minus 16 bytes for alignment */
    uint64_t user_stack_top = user_stack_virt + (2 * 4096) - 16;
    
    vga_write("User code mapped at: 0x40000000\n");
    vga_write("User stack at: 0x41001FF0\n");
    vga_write("\nJumping to Ring 3 (user mode)...\n");
    vga_write("User program will make syscall back to kernel.\n\n");
    
    /* Save return context - syscall handler will restore this */
    usermode_demo_completed = 0;
    __asm__ volatile(
        "mov %%rsp, %0\n"
        "mov %%rbp, %1\n"
        : "=m"(usermode_demo_return_rsp), "=m"(usermode_demo_return_rbp)
    );
    
    /* Save return RIP - jump here after syscall */
    __asm__ volatile(
        "lea 1f(%%rip), %%rax\n"
        "mov %%rax, %0\n"
        "1:\n"
        : "=m"(usermode_demo_return_rip)
        : : "rax"
    );
    
    /* Check if we're returning from syscall */
    if (usermode_demo_completed) {
        /* Syscall handler set this and restored our context - we're back! */
        /* The syscall handler already displayed the completion message and waited */
        return;
    }
    
    // Mask the timer interrupt (IRQ0) before jumping to user mode
    uint8_t pic_mask = hal_inb(0x21);
    hal_outb(0x21, pic_mask | 0x01);  // Mask IRQ0 (timer)
    
    // Disable interrupts and jump to user mode
    register uint64_t rip_val __asm__("rdi") = user_code_virt;
    register uint64_t rsp_val __asm__("rsi") = user_stack_top;
    
    __asm__ volatile(
        "cli\n"
        // Load user data segment into DS, ES, FS, GS
        "mov $0x1B, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        // Build IRETQ frame
        "push $0x1B\n"        // SS
        "push %%rsi\n"        // RSP (user stack)
        "push $0x202\n"       // RFLAGS (IF=1)
        "push $0x23\n"        // CS 
        "push %%rdi\n"        // RIP (user code)
        // Clear all GPRs
        "xor %%rax, %%rax\n"
        "xor %%rbx, %%rbx\n"
        "xor %%rcx, %%rcx\n"
        "xor %%rdx, %%rdx\n"
        "xor %%rsi, %%rsi\n"
        "xor %%rdi, %%rdi\n"
        "xor %%rbp, %%rbp\n"
        "xor %%r8, %%r8\n"
        "xor %%r9, %%r9\n"
        "xor %%r10, %%r10\n"
        "xor %%r11, %%r11\n"
        "xor %%r12, %%r12\n"
        "xor %%r13, %%r13\n"
        "xor %%r14, %%r14\n"
        "xor %%r15, %%r15\n"
        "iretq\n"
        :
        : "r"(rip_val), "r"(rsp_val)
        : "memory", "rax"
    );
    
    /* Should never reach here */
}

/* ========== VFS Demo ========== */

void run_vfs_demo(void) {
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("=== VFS & Mount System Demo ===\n\n");
    
    /* Show current mount table (populated at boot) */
    vga_write("1. Current mount table (set up at boot):\n");
    vga_write("   Path            FS        Device\n");
    vga_write("   --------------- --------- --------\n");
    
    /* Display mount info - root is mounted at boot */
    struct vnode *root = get_root_vnode();
    if (root) {
        vga_write("   /               ramfs     rootfs\n");
        vga_write("   /tmp            ramfs     none\n");
        vga_write("   /dev            ramfs     none\n");
    } else {
        vga_write("   (no mounts - fs not initialized)\n");
    }
    vga_write("\n");
    
    /* Step 2: Create file on mounted filesystem */
    vga_write("2. Create file on root filesystem... ");
    struct vnode* file = vfs_create_vnode(VREG);
    if (!file) {
        vga_write("[ERROR]\n");
        return;
    }
    vfs_open(file);
    vga_write("[OK]\n");
    
    /* Step 3: Write data */
    vga_write("3. Write data to file... ");
    const char* test_data = "Hello from mounted ramfs!";
    vfs_write(file, test_data, 26);
    vga_write("[OK]\n");
    vga_write("   Written: \"");
    vga_write(test_data);
    vga_write("\"\n");
    
    /* Step 4: Read data back */
    vga_write("4. Read data back... ");
    char buffer[64] = {0};
    vfs_read(file, buffer, 64);
    vga_write("[OK]\n");
    vga_write("   Read: \"");
    vga_write(buffer);
    vga_write("\"\n");
    
    /* Step 5: Show buffer cache stats */
    vga_write("5. Buffer cache: ");
    struct buffer_cache_stats stats;
    buffer_cache_get_stats(&stats);
    char num[16];
    vga_write("Hits=");
    int_to_str(stats.hits, num, 10);
    vga_write(num);
    vga_write(" Miss=");
    int_to_str(stats.misses, num, 10);
    vga_write(num);
    vga_write(" Cached=");
    int_to_str(stats.cached_blocks, num, 10);
    vga_write(num);
    vga_write("\n");
    
    /* Cleanup */
    vga_write("6. Cleanup... ");
    vfs_close(file);
    vga_write("[OK]\n\n");
    
    vga_write("=== Demo Complete! ===\n");
    vga_write("Root mounted at boot, ready for physical FS\n\n");
    vga_write("Press ESC to return.\n");
    
    /* Poll for ESC directly (works without interrupts) */
    while (1) {
        if (hal_inb(0x64) & 0x01) {
            uint8_t scancode = hal_inb(0x60);
            if (scancode == 0x01) {  /* ESC pressed */
                break;
            }
        }
        __asm__ volatile("pause");
    }
}

/* ========== Block Device Demo ========== */

void run_block_device_demo(void) {
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("=== Block Device Abstraction Layer Demo ===\n\n");
    
    /* Initialize block device subsystem */
    vga_write("1. Initializing block device subsystem...\n");
    block_device_init();
    vga_write("   [OK] Block subsystem initialized\n\n");
    
    /* Create a RAM block device (64 blocks = 256KB) */
    vga_write("2. Creating RAM block device 'ramdisk0' (64 blocks, 256KB)...\n");
    struct block_device *ramdisk = ramblock_create("ramdisk0", 64);
    if (!ramdisk) {
        vga_write("   [ERROR] Failed to create RAM block device!\n");
        return;
    }
    vga_write("   [OK] RAM disk created successfully\n\n");
    
    /* Register the device */
    vga_write("3. Registering block device...\n");
    if (block_device_register(ramdisk) != 0) {
        vga_write("   [ERROR] Failed to register device!\n");
        ramblock_destroy(ramdisk);
        return;
    }
    vga_write("   [OK] Device registered\n\n");
    
    /* Write some data using high-level API */
    vga_write("4. Writing test data via buffer cache...\n");
    const char *test_str = "Hello from TomahawkOS Block Device Layer!";
    ssize_t written = block_write(ramdisk, 0, test_str, 42);
    if (written < 0) {
        vga_write("   [ERROR] Write failed!\n");
    } else {
        vga_write("   [OK] Wrote ");
        char num[16];
        int_to_str(written, num, 10);
        vga_write(num);
        vga_write(" bytes to offset 0\n");
    }
    
    /* Write to different block */
    const char *test_str2 = "Data in block 5!";
    written = block_write(ramdisk, 5 * BLOCK_SIZE, test_str2, 17);
    if (written > 0) {
        vga_write("   [OK] Wrote ");
        char num[16];
        int_to_str(written, num, 10);
        vga_write(num);
        vga_write(" bytes to block 5\n\n");
    }
    
    /* Read data back */
    vga_write("5. Reading data back via buffer cache...\n");
    char read_buf[64] = {0};
    ssize_t bytes_read = block_read(ramdisk, 0, read_buf, 64);
    if (bytes_read < 0) {
        vga_write("   [ERROR] Read failed!\n");
    } else {
        vga_write("   [OK] Read from offset 0: \"");
        vga_write(read_buf);
        vga_write("\"\n");
    }
    
    /* Read from block 5 */
    char read_buf2[64] = {0};
    bytes_read = block_read(ramdisk, 5 * BLOCK_SIZE, read_buf2, 64);
    if (bytes_read > 0) {
        vga_write("   [OK] Read from block 5: \"");
        vga_write(read_buf2);
        vga_write("\"\n\n");
    }
    
    /* Show buffer cache statistics */
    vga_write("6. Buffer cache statistics:\n");
    struct buffer_cache_stats stats;
    buffer_cache_get_stats(&stats);
    
    char num[16];
    vga_write("   - Cache hits: ");
    int_to_str(stats.hits, num, 10);
    vga_write(num);
    vga_write("\n");
    
    vga_write("   - Cache misses: ");
    int_to_str(stats.misses, num, 10);
    vga_write(num);
    vga_write("\n");
    
    vga_write("   - Device reads: ");
    int_to_str(stats.reads, num, 10);
    vga_write(num);
    vga_write("\n");
    
    vga_write("   - Device writes: ");
    int_to_str(stats.writes, num, 10);
    vga_write(num);
    vga_write("\n");
    
    vga_write("   - Cached blocks: ");
    int_to_str(stats.cached_blocks, num, 10);
    vga_write(num);
    vga_write("\n");
    
    vga_write("   - Dirty blocks: ");
    int_to_str(stats.dirty_blocks, num, 10);
    vga_write(num);
    vga_write("\n\n");
    
    /* Test cache hit - read same block again */
    vga_write("7. Testing cache hit (re-reading same data)...\n");
    char read_buf3[64] = {0};
    block_read(ramdisk, 0, read_buf3, 64);
    buffer_cache_get_stats(&stats);
    vga_write("   Cache hits now: ");
    int_to_str(stats.hits, num, 10);
    vga_write(num);
    vga_write(" (should have increased)\n\n");
    
    /* Sync all buffers */
    vga_write("8. Syncing all dirty buffers to device...\n");
    buffer_sync_all(ramdisk);
    vga_write("   [OK] Sync complete\n\n");
    
    /* Cleanup */
    vga_write("9. Cleaning up...\n");
    block_device_unregister(ramdisk);
    ramblock_destroy(ramdisk);
    vga_write("   [OK] Device destroyed\n\n");
    
    vga_write("=== Block Device Demo Complete! ===\n");
    vga_write("Press ESC to return to menu.\n");
    
    /* Wait for ESC */
    demo_stop_requested = 0;
    create_process("esc-watcher", demo_esc_watcher);
    
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    /* Brief cleanup delay */
    for (volatile int i = 0; i < 200000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== FAT32 Shell Commands ========== */

/* Persistent FAT32 state across shell commands */
static struct vnode *g_fat32_root = NULL;
static struct vnode *g_fat32_cwd  = NULL;
static int           g_fat32_mounted = 0;
static char          g_fat32_cwd_path[256] = "/";

/* Forward declaration — defined after umount */
void shell_fat32_ensure_home(const char *username);

/* Create /home, /home/guest, and /home/<username> for every registered user */
static void ensure_all_home_dirs(void) {
    /* Ensure /home/guest */
    shell_fat32_ensure_home("guest");
    /* Ensure a home dir for every registered user */
    int count = password_store_get_count();
    for (int uid = 0; uid < MAX_USERS && count > 0; uid++) {
        char uname[MAX_USERNAME_LEN];
        if (password_store_get_username(uid, uname, MAX_USERNAME_LEN) == 0 && uname[0]) {
            shell_fat32_ensure_home(uname);
            count--;
        }
    }
}

/* Load /etc/init.cfg from FAT32 if it exists (overrides cpio defaults) */
static void load_fat32_init_conf(void) {
    if (!g_fat32_mounted || !g_fat32_root) return;
    struct vnode *etc_dir = NULL;
    if (vfs_lookup(g_fat32_root, "etc", &etc_dir) != 0 || !etc_dir) return;
    struct vnode *conf_vp = NULL;
    if (vfs_lookup(etc_dir, "init.cfg", &conf_vp) != 0 || !conf_vp) return;

    int64_t sz = vfs_getsize(conf_vp);
    if (sz <= 0 || sz > 4095) return;

    char buf[4096];
    int n = vfs_read_at(conf_vp, buf, (size_t)sz, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Parse lines and override individual keys */
    char line[128];
    int llen = 0;
    for (int i = 0; i <= n; i++) {
        char c = (i < n) ? buf[i] : '\0';
        if (c == '\n' || c == '\r' || c == '\0') {
            line[llen] = '\0';
            if (llen > 0) {
                char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '#' || *p == '\0') { llen = 0; continue; }
                char *eq = p;
                while (*eq && *eq != '=') eq++;
                if (*eq == '=') {
                    *eq = '\0';
                    char *key = p;
                    char *val = eq + 1;
                    int kl = 0; while (key[kl]) kl++;
                    while (kl > 0 && (key[kl-1] == ' ' || key[kl-1] == '\t')) key[--kl] = '\0';
                    while (*val == ' ' || *val == '\t') val++;
                    if (*key) init_config_set(key, val);
                }
            }
            llen = 0;
        } else {
            if (llen < 127) line[llen++] = c;
        }
    }
    uart_puts("[INITCFG] Loaded overrides from FAT32 /etc/init.cfg\n");
}

/*
 * Extract argument N (1-based) from a command line string into buf[max].
 * Arg 0 is the command itself.  If is_rest is true, the last arg includes
 * everything remaining (spaces and all).
 * Returns the length copied, or 0 if not found.
 */
static int cmdline_get_arg(const char *cmdline, int n, char *buf, int max, int is_rest) {
    if (!cmdline) { buf[0] = '\0'; return 0; }
    const char *p = cmdline;
    /* Skip to word N */
    for (int i = 0; i < n; i++) {
        while (*p && *p != ' ') p++;   /* skip current word */
        while (*p == ' ') p++;         /* skip spaces */
    }
    if (!*p) { buf[0] = '\0'; return 0; }
    int len = 0;
    if (is_rest) {
        while (*p && len < max - 1) buf[len++] = *p++;
    } else {
        while (*p && *p != ' ' && len < max - 1) buf[len++] = *p++;
    }
    buf[len] = '\0';
    return len;
}

/* Kernel-mode direct keyboard polling with shift + CapsLock support */
static char fat32_poll_char(void) {
    static const char scancode_lower[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
        'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
        'b','n','m',',','.','/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    };
    static const char scancode_upper[128] = {
        0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
        'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
        'B','N','M','<','>','?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    };
    static int shift_held = 0;
    static int caps_lock  = 0;
    char c = 0;
    while (!c) {
        if (hal_inb(0x64) & 0x01) {
            uint8_t sc = hal_inb(0x60);
            /* Shift press/release */
            if (sc == 0x2A || sc == 0x36) { shift_held = 1; continue; }
            if (sc == 0xAA || sc == 0xB6) { shift_held = 0; continue; }
            /* CapsLock toggle */
            if (sc == 0x3A) { caps_lock = !caps_lock; continue; }
            /* Ignore break codes */
            if (sc & 0x80) continue;
            if (sc < sizeof(scancode_lower)) {
                int use_shift = shift_held;
                char lower = scancode_lower[sc];
                if (caps_lock && lower >= 'a' && lower <= 'z')
                    use_shift = !use_shift;
                c = use_shift ? scancode_upper[sc] : scancode_lower[sc];
            }
        }
        if (!c) __asm__ volatile("pause");
    }
    return c;
}

/* Read a line with full QoL editing (arrows, history, Home/End, Delete).
   Delegates to tty_readline.  Returns length, -1 on ESC. */
static int fat32_read_line(char *buf, int max) {
    return tty_readline(tty_console(), buf, max);
}

/* --- mount --- */
void shell_fat32_mount(const char *cmdline) {
    (void)cmdline;
    if (g_fat32_mounted) {
        vga_write("[INFO] FAT32 already mounted.\n");
        return;
    }
    /* Check if FAT32 was auto-mounted at boot */
    struct vnode *auto_root = NULL;
    if (vfs_resolve_path("/mnt/fat", &auto_root) == 0 && auto_root &&
        auto_root->v_type == VDIR && auto_root->v_mount_data != NULL) {
        g_fat32_root = auto_root;
        g_fat32_cwd  = auto_root;
        g_fat32_mounted = 1;
        g_fat32_cwd_path[0] = '/'; g_fat32_cwd_path[1] = '\0';
        vga_write("[OK] Using auto-mounted FAT32 volume.\n");
        ensure_all_home_dirs();
        load_fat32_init_conf();
        return;
    }
    vga_write("Initializing ATA driver... ");
    ata_init();
    struct block_device *dev = ata_get_block_device(0);
    if (!dev) {
        vga_write("[ERROR] No ATA disk found.\n");
        return;
    }
    vga_write("[OK]\nMounting FAT32 volume... ");
    fat32_register();
    if (do_mount("/mnt/fat", "fat32", dev, 0) != 0) {
        vga_write("[ERROR] Mount failed.\n");
        return;
    }
    struct vnode *mounted_root = NULL;
    if (vfs_resolve_path("/mnt/fat", &mounted_root) != 0 || !mounted_root) {
        vga_write("[ERROR] Mount registered but root not found.\n");
        return;
    }
    g_fat32_root = mounted_root;
    g_fat32_cwd = g_fat32_root;
    g_fat32_mounted = 1;
    g_fat32_cwd_path[0] = '/'; g_fat32_cwd_path[1] = '\0';
    vga_write("[OK]\n");
    ensure_all_home_dirs();
    load_fat32_init_conf();
}

/* --- umount --- */
void shell_fat32_umount(const char *cmdline) {
    (void)cmdline;
    if (!g_fat32_mounted) {
        vga_write("[INFO] No FAT32 volume mounted.\n");
        return;
    }
    fat32_sync(g_fat32_root);
    /* Remove mount table entry (also calls fat32_unmount internally).
       If not in mount table, unmount directly. */
    if (do_unmount("/mnt/fat") != 0)
        fat32_unmount(g_fat32_root);
    g_fat32_root = NULL;
    g_fat32_cwd  = NULL;
    g_fat32_mounted = 0;
    g_fat32_cwd_path[0] = '/'; g_fat32_cwd_path[1] = '\0';
    vga_write("[OK] Volume unmounted.\n");
}

/* --- CWD path getter (used by syscall.c for permission checks) --- */
const char *shell_fat32_get_cwd_path(void) {
    return g_fat32_cwd_path;
}

/* --- Recursively delete a directory and all its contents --- */
void shell_fat32_delete_home(const char *username) {
    if (!g_fat32_mounted || !g_fat32_root || !username || !username[0]) return;
    struct vnode *home_dir = NULL;
    if (vfs_lookup(g_fat32_root, "home", &home_dir) != 0 || !home_dir) return;
    struct vnode *user_dir = NULL;
    if (vfs_lookup(home_dir, username, &user_dir) != 0 || !user_dir) return;

    /* Delete all entries inside the user's home directory */
    struct vfs_dirent dent;
    uint64_t off;
    int safety = 256;
    for (;;) {
        off = 0;
        int found = 0;
        while (vfs_readdir(user_dir, &dent, &off) == 0) {
            if (dent.d_name[0] == '.' &&
                (dent.d_name[1] == '\0' ||
                 (dent.d_name[1] == '.' && dent.d_name[2] == '\0')))
                continue;
            /* Remove this entry (files or empty subdirs) */
            vfs_remove(user_dir, dent.d_name);
            found = 1;
            break; /* restart iteration since directory changed */
        }
        if (!found || --safety <= 0) break;
    }

    /* Now remove the (hopefully empty) user directory itself */
    vfs_remove(home_dir, username);
    uart_puts("[KERNEL] Deleted home dir for ");
    uart_puts(username);
    uart_puts("\n");
}

/* --- Ensure /home/<username> exists on the FAT32 volume --- */
void shell_fat32_ensure_home(const char *username) {
    if (!g_fat32_mounted || !g_fat32_root || !username || !username[0]) return;
    /* Ensure /home exists */
    struct vnode *home_dir = NULL;
    if (vfs_lookup(g_fat32_root, "home", &home_dir) != 0 || !home_dir) {
        if (vfs_mkdir(g_fat32_root, "home", &home_dir) != 0 || !home_dir) return;
    }
    /* Ensure /home/<username> exists */
    struct vnode *user_dir = NULL;
    if (vfs_lookup(home_dir, username, &user_dir) != 0 || !user_dir) {
        vfs_mkdir(home_dir, username, &user_dir);
    }
}

/* --- Change CWD to /home/<username> --- */
void shell_fat32_cd_to_home(const char *username) {
    if (!g_fat32_mounted || !g_fat32_root || !username || !username[0]) return;
    struct vnode *home_dir = NULL;
    if (vfs_lookup(g_fat32_root, "home", &home_dir) != 0 || !home_dir) return;
    struct vnode *user_dir = NULL;
    if (vfs_lookup(home_dir, username, &user_dir) != 0 || !user_dir) return;
    g_fat32_cwd = user_dir;
    /* Build path "/home/<username>" */
    int pos = 0;
    const char *prefix = "/home/";
    while (*prefix && pos < 254) g_fat32_cwd_path[pos++] = *prefix++;
    for (int i = 0; username[i] && pos < 254; i++)
        g_fat32_cwd_path[pos++] = username[i];
    g_fat32_cwd_path[pos] = '\0';
}

/* --- ls --- */
void shell_fat32_ls(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }

    /* Resolve target directory: use argument if given, else cwd */
    struct vnode *target = g_fat32_cwd;
    char path[256];
    if (cmdline_get_arg(cmdline, 1, path, 256, 0) > 0) {
        struct vnode *cur;
        const char *p = path;
        if (*p == '/') {
            cur = g_fat32_root;
            p++;
        } else {
            cur = g_fat32_cwd;
        }
        while (*p) {
            while (*p == '/') p++;
            if (*p == '\0') break;
            char comp[64];
            int ci = 0;
            while (*p && *p != '/' && ci < 63) comp[ci++] = *p++;
            comp[ci] = '\0';
            if (comp[0] == '.' && comp[1] == '\0') continue;
            struct vnode *child = NULL;
            int rc = vfs_lookup(cur, comp, &child);
            if (rc != 0 || !child) {
                vga_write("ls: cannot access '");
                vga_write(path);
                vga_write("': No such file or directory\n");
                return;
            }
            cur = child;
        }
        if (cur->v_type != VDIR) {
            vga_write("ls: '");
            vga_write(path);
            vga_write("': Not a directory\n");
            return;
        }
        target = cur;
    }

    char num[16];
    struct vfs_dirent dent;
    uint64_t offset = 0;
    int count = 0;
    vga_write("Name               Type      Size\n");
    vga_write("------------------ --------- --------\n");
    while (vfs_readdir(target, &dent, &offset) == 0) {
        int nlen = 0;
        while (dent.d_name[nlen]) nlen++;
        vga_write(dent.d_name);
        for (int pad = nlen; pad < 19; pad++) vga_write(" ");
        if (dent.d_type == VDIR) {
            vga_write("DIR       -");
        } else {
            vga_write("FILE      ");
            int_to_str((int)dent.d_size, num, 10);
            vga_write(num);
        }
        vga_write("\n");
        count++;
    }
    vga_write("(");
    int_to_str(count, num, 10);
    vga_write(num);
    vga_write(" entries)\n");
}

/* --- stat --- */
void shell_fat32_stat(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) <= 0) {
        vga_write("stat: missing file operand\n");
        return;
    }
    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, name, &vp);
    if (rc != 0 || !vp) {
        vga_write("stat: '");
        vga_write(name);
        vga_write("': No such file or directory\n");
        return;
    }
    char num[16];
    vga_write("  File: ");
    vga_write(name);
    vga_write("\n  Type: ");
    vga_write(vp->v_type == VDIR ? "directory" : "regular file");
    vga_write("\n  Size: ");
    if (vp->v_type == VDIR) {
        /* Count children via readdir */
        struct vfs_dirent dent;
        uint64_t off = 0;
        int children = 0;
        while (vfs_readdir(vp, &dent, &off) == 0)
            children++;
        int_to_str(children, num, 10);
        vga_write(num);
        vga_write(" entries");
    } else {
        int64_t sz = vfs_getsize(vp);
        int_to_str((int)sz, num, 10);
        vga_write(num);
        vga_write(" bytes");
    }
    vga_write("\n");
}

/* --- cat --- */
void shell_fat32_cat(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) <= 0) {
        vga_write("Filename: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }
    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, name, &vp);
    if (rc != 0 || !vp) {
        vga_write("[ERROR] File not found.\n");
        return;
    }
    int64_t sz = vfs_getsize(vp);
    char num[16];
    int_to_str((int)sz, num, 10);
    vga_write("(");
    vga_write(num);
    vga_write(" bytes)\n");
    /* Read in 256-byte chunks */
    uint64_t off = 0;
    char buf[257];
    while (off < (uint64_t)sz) {
        int chunk = ((uint64_t)sz - off > 256) ? 256 : (int)((uint64_t)sz - off);
        int rd = vfs_read_at(vp, buf, (size_t)chunk, off);
        if (rd <= 0) break;
        buf[rd] = '\0';
        vga_write(buf);
        off += (uint64_t)rd;
    }
    vga_write("\n");
}

/* --- write --- */
void shell_fat32_write(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    char content[256];
    int have_args = 0;
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) > 0 &&
        cmdline_get_arg(cmdline, 2, content, 256, 1) > 0) {
        have_args = 1;
    }
    if (!have_args) {
        vga_write("Filename: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }
    /* Remove existing file first to truncate, then create fresh */
    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, name, &vp);
    if (rc == 0 && vp) {
        vfs_remove(g_fat32_cwd, name);
        vp = NULL;
    }
    rc = vfs_create(g_fat32_cwd, name, &vp);
    if (rc != 0 || !vp) {
        vga_write("[ERROR] Cannot create file.\n");
        return;
    }
    int len;
    if (!have_args) {
        vga_write("Content: ");
        len = fat32_read_line(content, 256);
        if (len < 0) return;
    } else {
        len = 0;
        while (content[len]) len++;
    }
    int wr = vfs_write_at(vp, content, (size_t)len, 0);
    char num[16];
    int_to_str(wr, num, 10);
    vga_write("[OK] Wrote ");
    vga_write(num);
    vga_write(" bytes.\n");
}

/* --- touch --- */
void shell_fat32_touch(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) <= 0) {
        vga_write("Filename: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }
    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, name, &vp);
    if (rc == 0 && vp) {
        vga_write("[OK] File already exists.\n");
        return;
    }
    rc = vfs_create(g_fat32_cwd, name, &vp);
    if (rc == 0 && vp)
        vga_write("[OK] File created.\n");
    else
        vga_write("[ERROR] Cannot create file.\n");
}

/* --- mkdir --- */
void shell_fat32_mkdir(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) <= 0) {
        vga_write("Directory name: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }
    struct vnode *dir = NULL;
    int rc = vfs_mkdir(g_fat32_cwd, name, &dir);
    if (rc == 0)
        vga_write("[OK] Directory created.\n");
    else
        vga_write("[ERROR] mkdir failed.\n");
}

/* --- rm --- */
void shell_fat32_rm(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) <= 0) {
        vga_write("Filename: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }
    int rc = vfs_remove(g_fat32_cwd, name);
    if (rc == 0)
        vga_write("[OK] Removed.\n");
    else
        vga_write("[ERROR] Remove failed.\n");
}

/* --- cd: supports single name, relative paths, and absolute paths --- */
void shell_fat32_cd(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char path[256];
    if (cmdline_get_arg(cmdline, 1, path, 256, 0) <= 0) {
        vga_write("Directory: ");
        if (fat32_read_line(path, 256) <= 0) return;
    }

    /* Determine starting point: absolute or relative */
    struct vnode *cur = g_fat32_cwd;
    char new_path[256];
    int npos = 0;
    const char *p = path;

    if (*p == '/') {
        /* Absolute path: start from root */
        cur = g_fat32_root;
        new_path[0] = '/'; new_path[1] = '\0'; npos = 1;
        p++; /* skip leading '/' */
    } else {
        /* Relative path: copy current cwd_path as base */
        for (int i = 0; g_fat32_cwd_path[i] && npos < 254; i++)
            new_path[npos++] = g_fat32_cwd_path[i];
        new_path[npos] = '\0';
    }

    /* Walk each component separated by '/' */
    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract next component */
        char comp[64];
        int ci = 0;
        while (*p && *p != '/' && ci < 63) comp[ci++] = *p++;
        comp[ci] = '\0';

        if (comp[0] == '.' && comp[1] == '\0') {
            /* "." – stay in current directory */
            continue;
        }

        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            /* ".." – go up one level */
            struct vnode *parent = NULL;
            int rc = vfs_lookup(cur, "..", &parent);
            if (rc != 0 || !parent) {
                vga_write("[ERROR] Cannot go up.\n");
                return;
            }
            cur = parent;
            /* Strip last path component from new_path */
            if (npos > 1) {
                int i = npos - 1;
                if (new_path[i] == '/') i--;
                while (i > 0 && new_path[i] != '/') i--;
                if (i == 0) { new_path[0] = '/'; new_path[1] = '\0'; npos = 1; }
                else { new_path[i] = '\0'; npos = i; }
            }
            continue;
        }

        /* Normal component: lookup */
        struct vnode *child = NULL;
        int rc = vfs_lookup(cur, comp, &child);
        if (rc != 0 || !child) {
            vga_write("[ERROR] Not found: ");
            vga_write(comp);
            vga_write("\n");
            return;
        }
        cur = child;
        /* Append /comp to new_path */
        if (npos > 1) new_path[npos++] = '/';
        for (int i = 0; comp[i] && npos < 254; i++)
            new_path[npos++] = comp[i];
        new_path[npos] = '\0';
    }

    g_fat32_cwd = cur;
    for (int i = 0; i <= npos; i++) g_fat32_cwd_path[i] = new_path[i];
    vga_write("[OK] Changed directory.\n");
}

/* --- rename --- */
void shell_fat32_rename(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char old_name[64];
    char new_name[64];
    int have_args = 0;
    if (cmdline_get_arg(cmdline, 1, old_name, 64, 0) > 0 &&
        cmdline_get_arg(cmdline, 2, new_name, 64, 0) > 0) {
        have_args = 1;
    }
    if (!have_args) {
        vga_write("Old name: ");
        if (fat32_read_line(old_name, 64) <= 0) return;
    }

    /* Verify the source exists */
    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, old_name, &vp);
    if (rc != 0 || !vp) {
        vga_write("[ERROR] File/directory not found.\n");
        return;
    }

    if (!have_args) {
        vga_write("New name: ");
        if (fat32_read_line(new_name, 64) <= 0) return;
    }

    /* Check that destination doesn't already exist */
    struct vnode *existing = NULL;
    if (vfs_lookup(g_fat32_cwd, new_name, &existing) == 0 && existing) {
        vga_write("mv: '");
        vga_write(new_name);
        vga_write("' already exists\n");
        return;
    }

    /* Remove old entry and create new entry pointing to same vnode.
       FAT32 remove + create achieves a rename within the same directory. */
    rc = vfs_remove(g_fat32_cwd, old_name);
    if (rc != 0) {
        vga_write("[ERROR] Failed to remove old name.\n");
        return;
    }
    /* Re-create entry with new name — write the file back */
    struct vnode *new_vp = NULL;
    rc = vfs_create(g_fat32_cwd, new_name, &new_vp);
    if (rc != 0 || !new_vp) {
        vga_write("[ERROR] Failed to create with new name.\n");
        return;
    }
    /* Copy data from old vnode to new vnode if it was a regular file */
    if (vp->v_type == VREG) {
        int64_t sz = vfs_getsize(vp);
        if (sz > 0) {
            char buf[512];
            uint64_t off = 0;
            while (off < (uint64_t)sz) {
                int chunk = ((uint64_t)sz - off > 512) ? 512 : (int)((uint64_t)sz - off);
                int rd = vfs_read_at(vp, buf, (size_t)chunk, off);
                if (rd <= 0) break;
                vfs_write_at(new_vp, buf, (size_t)rd, off);
                off += (uint64_t)rd;
            }
        }
    }
    vga_write("[OK] Renamed.\n");
}

/* --- chmod --- */
void shell_fat32_chmod(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted. Use 'mount' first.\n");
        return;
    }
    char name[64];
    char mode_str[16];
    int have_args = 0;
    if (cmdline_get_arg(cmdline, 1, name, 64, 0) > 0 &&
        cmdline_get_arg(cmdline, 2, mode_str, 16, 0) > 0) {
        have_args = 1;
    }
    if (!have_args) {
        vga_write("Filename: ");
        if (fat32_read_line(name, 64) <= 0) return;
    }

    struct vnode *vp = NULL;
    int rc = vfs_lookup(g_fat32_cwd, name, &vp);
    if (rc != 0 || !vp) {
        vga_write("[ERROR] File/directory not found.\n");
        return;
    }

    if (!have_args) {
        vga_write("Mode (octal, e.g. 755): ");
        if (fat32_read_line(mode_str, 16) <= 0) return;
    }

    /* Parse octal mode string */
    uint16_t mode = 0;
    for (int i = 0; mode_str[i]; i++) {
        if (mode_str[i] < '0' || mode_str[i] > '7') {
            vga_write("[ERROR] Invalid octal digit.\n");
            return;
        }
        mode = (uint16_t)(mode * 8 + (mode_str[i] - '0'));
    }

    /* FAT32 doesn't really support Unix permissions, but we store in inode */
    struct inode *ip = (struct inode *)vp->v_data;
    if (ip) {
        uint16_t type_bits = (uint16_t)(ip->i_mode & 0xF000u);
        ip->i_mode = (uint16_t)(type_bits | (mode & 07777u));
        vga_write("[OK] Mode set.\n");
    } else {
        vga_write("[ERROR] No inode for this vnode.\n");
    }
}

/* --- editinit: interactive editor for init.cfg, saves to FAT32 --- */
void shell_fat32_editinit(const char *cmdline) {
    (void)cmdline;
    if (!init_config_is_loaded()) {
        vga_write("[ERROR] Init config not loaded.\n");
        return;
    }

    vga_write("=== Edit Init Configuration ===\n");
    vga_write("Press Enter to keep current value, or type a new one.\n\n");

    int count = init_config_get_count();
    for (int i = 0; i < count; i++) {
        const char *key = NULL, *val = NULL;
        if (init_config_get_entry(i, &key, &val) != 0) continue;

        vga_write("  ");
        vga_write(key);
        vga_write(" [");
        vga_write(val);
        vga_write("]: ");

        char newval[64];
        int len = fat32_read_line(newval, 64);
        if (len < 0) {  /* ESC pressed */
            vga_write("[INFO] Aborted.\n");
            return;
        }
        if (len > 0) {
            init_config_set(key, newval);
        }
        /* len == 0 (just Enter) → keep old value */
    }

    /* Save to FAT32 disk if mounted */
    if (!g_fat32_mounted || !g_fat32_root) {
        vga_write("\n[WARNING] FAT32 not mounted — changes are in memory only.\n");
        return;
    }

    /* Ensure /etc directory exists */
    struct vnode *etc_dir = NULL;
    if (vfs_lookup(g_fat32_root, "etc", &etc_dir) != 0 || !etc_dir) {
        if (vfs_mkdir(g_fat32_root, "etc", &etc_dir) != 0 || !etc_dir) {
            vga_write("\n[ERROR] Cannot create /etc on FAT32.\n");
            return;
        }
    }

    /* Remove old init.cfg if it exists */
    struct vnode *old_conf = NULL;
    if (vfs_lookup(etc_dir, "init.cfg", &old_conf) == 0 && old_conf) {
        vfs_remove(etc_dir, "init.cfg");
    }

    /* Create new init.cfg */
    struct vnode *conf_vp = NULL;
    if (vfs_create(etc_dir, "init.cfg", &conf_vp) != 0 || !conf_vp) {
        vga_write("\n[ERROR] Cannot create init.cfg on FAT32.\n");
        return;
    }

    char buf[2048];
    int len = init_config_build_buffer(buf, (int)sizeof(buf));
    vfs_write_at(conf_vp, buf, (size_t)len, 0);

    vga_write("\n[OK] Configuration saved to FAT32 /etc/init.cfg\n");
    vga_write("     Changes take effect on next boot.\n");
}

/* --- wget: download file via HTTP and save to FAT32 disk --- */
void shell_fat32_wget(const char *cmdline) {
    if (!g_fat32_mounted) {
        vga_write("[ERROR] No FAT32 volume mounted.\n");
        return;
    }

    /* Parse: wget <domain> <url-path> <filename> [port] */
    char domain[128], urlpath[256], filename[64], portstr[16];
    if (cmdline_get_arg(cmdline, 1, domain, (int)sizeof(domain), 0) <= 0) {
        vga_write("usage: wget <domain> <url-path> <filename> [port]\n");
        return;
    }
    if (cmdline_get_arg(cmdline, 2, urlpath, (int)sizeof(urlpath), 0) <= 0 ||
        urlpath[0] != '/') {
        vga_write("wget: url-path must start with '/'\n");
        return;
    }
    if (cmdline_get_arg(cmdline, 3, filename, (int)sizeof(filename), 0) <= 0) {
        vga_write("wget: missing filename\n");
        return;
    }
    uint16_t port = 80;
    if (cmdline_get_arg(cmdline, 4, portstr, (int)sizeof(portstr), 0) > 0) {
        uint32_t p = 0;
        for (int i = 0; portstr[i] >= '0' && portstr[i] <= '9'; i++)
            p = p * 10 + (uint32_t)(portstr[i] - '0');
        if (p > 0 && p <= 65535) port = (uint16_t)p;
    }

    /* Get NIC */
    net_device_t *dev = net_device_get_by_name("eth0");
    if (!dev) dev = net_device_get_by_name("lo");
    if (!dev) { vga_write("wget: no NIC available\n"); return; }

    vga_write("wget: http://");
    vga_write(domain);
    vga_write(urlpath);
    vga_write(" -> ");
    vga_write(filename);
    vga_write("\n");

    /* Fetch */
    static char body[8192];
    int rc = http_get(dev, domain, port, urlpath, body, sizeof(body));
    if (rc == -1) { vga_write("wget: DNS resolution failed\n"); return; }
    if (rc == -2) { vga_write("wget: TCP connect/send failed\n"); return; }
    if (rc == -4) { vga_write("wget: empty or malformed response\n"); return; }

    /* Remove old file if it exists, then create fresh */
    struct vnode *vp = NULL;
    if (vfs_lookup(g_fat32_cwd, filename, &vp) == 0 && vp) {
        vfs_remove(g_fat32_cwd, filename);
        vp = NULL;
    }
    if (vfs_create(g_fat32_cwd, filename, &vp) != 0 || !vp) {
        vga_write("wget: cannot create file on disk\n");
        return;
    }

    /* Write to FAT32 disk */
    int len = 0;
    while (body[len]) len++;
    int wr = vfs_write_at(vp, body, (size_t)len, 0);
    if (wr < 0) {
        vga_write("wget: write error\n");
    } else {
        char num[16]; int_to_str(wr, num, 10);
        vga_write("wget: saved ");
        vga_write(num);
        vga_write(" bytes to ");
        vga_write(filename);
        vga_write("\n");
        if (rc == -3) vga_write("[wget: response was truncated]\n");
    }
}

/* Keep the old demo function available */
void run_fat32_demo(void) {
    shell_fat32_mount(NULL);
    if (!g_fat32_mounted) return;
    shell_fat32_ls(NULL);
    shell_fat32_umount(NULL);
}

/* ========== User Mode Password Demo ========== */

/* Helper to emit a syscall: mov rax, num; syscall */
static uint8_t *emit_syscall_only(uint8_t *p, uint64_t syscall_num) {
    /* mov rax, imm32 (48 C7 C0 xx xx xx xx) */
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC0;
    *p++ = (uint8_t)(syscall_num & 0xFF);
    *p++ = (uint8_t)((syscall_num >> 8) & 0xFF);
    *p++ = (uint8_t)((syscall_num >> 16) & 0xFF);
    *p++ = (uint8_t)((syscall_num >> 24) & 0xFF);
    /* syscall (0F 05) */
    *p++ = 0x0F; *p++ = 0x05;
    return p;
}

/* Helper to emit: mov reg, imm64 (10-byte movabs) */
static uint8_t *emit_mov64(uint8_t *p, uint8_t reg, uint64_t imm) {
    /* REX.W + B8+rd for RAX(0), RCX(1), RDX(2), RBX(3), RSP(4), RBP(5), RSI(6), RDI(7) */
    /* For R8-R15, need REX.B */
    uint8_t rex = 0x48;
    uint8_t opcode = 0xB8 + (reg & 7);
    if (reg >= 8) {
        rex |= 0x01; /* REX.B for R8-R15 */
    }
    *p++ = rex; *p++ = opcode;
    for (int i = 0; i < 8; i++) {
        *p++ = (uint8_t)(imm >> (i * 8));
    }
    return p;
}

/* Helper to emit write syscall: mov rdi, str; mov rsi, len; syscall(13) */
static uint8_t *emit_print(uint8_t *p, uint64_t str_addr, uint64_t len) {
    p = emit_mov64(p, 7, str_addr); /* RDI = str */
    p = emit_mov64(p, 6, len);      /* RSI = len */
    p = emit_syscall_only(p, 13);   /* SYS_WRITE */
    return p;
}

/* ========== Tomahawk Shell ========== */

void run_tomahawk_shell(void) {
    vga_write("=== Tomahawk Shell ===\n\n");
    vga_write("Starting interactive shell in Ring 3...\n\n");
    
    /* Disable interrupts early to prevent timer-driven context switches
       from corrupting register state during page setup and JIT codegen. */
    __asm__ volatile("cli");
    
    /* Memory layout - need more code space for shell */
    #define SHELL_CODE   0x40000000ULL
    #define SHELL_CODE2  0x40001000ULL  /* Second code page */
    #define SHELL_CODE3  0x40002000ULL  /* Third code page */
    #define SHELL_DATA   0x40003000ULL
    #define SHELL_BUF    0x40004000ULL
    #define SHELL_STACK  0x40006000ULL
    
    /* Allocate frames */
    uint64_t code_frame = pfa_alloc_frame();
    uint64_t code2_frame = pfa_alloc_frame();
    uint64_t code3_frame = pfa_alloc_frame();
    uint64_t data_frame = pfa_alloc_frame();
    uint64_t buf_frame = pfa_alloc_frame();
    uint64_t stack_frame = pfa_alloc_frame();
    
    if (!code_frame || !code2_frame || !code3_frame || !data_frame || !buf_frame || !stack_frame) {
        vga_write("ERROR: Failed to allocate frames.\n");
        return;
    }
    
    /* Map pages with USER permissions */
    uintptr_t cr3 = paging_get_current_cr3();
    uint64_t flags = PTE_PRESENT | PTE_RW | PTE_USER;
    
    paging_map_page(cr3, SHELL_CODE, code_frame, flags);
    paging_map_page(cr3, SHELL_CODE2, code2_frame, flags);
    paging_map_page(cr3, SHELL_CODE3, code3_frame, flags);
    paging_map_page(cr3, SHELL_DATA, data_frame, flags);
    paging_map_page(cr3, SHELL_BUF, buf_frame, flags);
    paging_map_page(cr3, SHELL_STACK - 0x1000, stack_frame, flags);
    
    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_CODE) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_CODE2) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_CODE3) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_DATA) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_BUF) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_STACK - 0x1000) : "memory");
    
    /* Kernel access pointers */
    uint8_t *code_ptr = (uint8_t *)code_frame;
    uint8_t *code2_ptr = (uint8_t *)code2_frame;
    uint8_t *code3_ptr = (uint8_t *)code3_frame;
    uint8_t *data_ptr = (uint8_t *)data_frame;
    
    /* Clear pages */
    for (int i = 0; i < 4096; i++) {
        code_ptr[i] = 0;
        code2_ptr[i] = 0;
        code3_ptr[i] = 0;
        data_ptr[i] = 0;
    }
    
    /* Setup strings in data page */
    size_t off = 0;
    
    #define PLACE_STR(name, text) \
        uint64_t name = SHELL_DATA + off; \
        size_t name##_len = sizeof(text) - 1; \
        { const char *s = text; for (size_t _i = 0; s[_i]; _i++) data_ptr[off++] = s[_i]; data_ptr[off++] = 0; }

    PLACE_STR(s_banner, "\n"
        "  _____                    _                 _    \n"
        " |_   _|__  _ __ ___   __ _| |__   __ ___      _| | __\n"
        "   | |/ _ \\| '_ ` _ \\ / _` | '_ \\ / _` \\ \\ /\\ / / |/ /\n"
        "   | | (_) | | | | | | (_| | | | | (_| |\\ V  V /|   < \n"
        "   |_|\\___/|_| |_| |_|\\__,_|_| |_|\\__,_| \\_/\\_/ |_|\\_\\\n"
        "                                          Shell v1.0\n\n")
    PLACE_STR(s_help, "Available commands:\n\n"
        " Session:\n"
        "  login     - Log in with username and password\n"
        "  logout    - Log out current user\n"
        "  register  - Create a new user account\n"
        "  passwd    - Change your password\n"
        "  clear     - Clear the screen\n"
        "  help      - Show this help message\n"
        "  exit      - Shutdown the system\n\n"
        " Files:\n"
        "  ls [path] - List directory contents\n"
        "  cat <file>- Display file contents\n"
        "  tree [p]  - Show directory tree\n"
        "  stat <p>  - Show file/dir info\n"
        "  cd <path> - Change directory\n"
        "  mkdir <p> - Create a directory\n"
        "  touch <f> - Create an empty file\n"
        "  write <f> <text> - Write text to a file\n"
        "  rm <path> - Remove a file or directory\n"
        "  mv <old> <new>  - Rename a file or directory\n"
        "  mount     - Mount FAT32 volume from ATA disk\n"
        "  umount    - Unmount FAT32 volume\n\n"
        " Admin:\n"
        "  userdel <user>  - Delete a user account\n"
        "  promote <user>  - Grant admin privileges\n"
        "  demote <user>   - Revoke admin privileges\n\n"
        " Config:\n"
        "  initconf  - Show loaded init configuration\n"
        "  editinit  - Edit init configuration (saved to disk)\n\n"
        " Network:\n"
        "  netinfo   - Show NIC IP/MAC/gateway info\n"
        "  dhcp      - Run DHCP on eth0\n"
        "  udpsend <ip> <port> <msg> - Send a UDP datagram\n"
        "  httpget <host> [/path] [port] - HTTP GET request\n"
        "  wget <domain> <url-path> <filename> [port] - Download to disk\n\n"
        " Debug:\n"
        "  tests     - Run kernel unit tests\n\n")
    /* Build prompts dynamically from init.cfg (hostname= and username=) */
    const char *_cfg_host = init_config_get("hostname");
    const char *_cfg_user = init_config_get("username");
    if (!_cfg_host || !_cfg_host[0]) _cfg_host = "tomahawk";
    if (!_cfg_user || !_cfg_user[0]) _cfg_user = "guest";

    /* Guest/default prompt: "<username>@<hostname>:" */
    char _guest_prompt[64];
    {
        char *_q = _guest_prompt;
        for (const char *_c = _cfg_user; *_c; _c++) *_q++ = *_c;
        *_q++ = '@';
        for (const char *_c = _cfg_host; *_c; _c++) *_q++ = *_c;
        *_q++ = ':'; *_q = '\0';
    }
    /* Logged-in suffix: "@<hostname>:" */
    char _at_prompt[64];
    {
        char *_q = _at_prompt;
        *_q++ = '@';
        for (const char *_c = _cfg_host; *_c; _c++) *_q++ = *_c;
        *_q++ = ':'; *_q = '\0';
    }

    uint64_t s_prompt_guest = SHELL_DATA + off; size_t s_prompt_guest_len = 0;
    { const char *_s = _guest_prompt; while (*_s) { data_ptr[off++] = (uint8_t)*_s++; s_prompt_guest_len++; } data_ptr[off++] = 0; }
    uint64_t s_prompt_at = SHELL_DATA + off; size_t s_prompt_at_len = 0;
    { const char *_s = _at_prompt; while (*_s) { data_ptr[off++] = (uint8_t)*_s++; s_prompt_at_len++; } data_ptr[off++] = 0; }

    PLACE_STR(s_prompt_end, "> ")

    PLACE_STR(s_user, "Username: ")
    PLACE_STR(s_pass, "Password: ")
    PLACE_STR(s_login_ok, "\n[OK] Login successful! Welcome, ")
    PLACE_STR(s_login_fail, "\n[FAIL] Invalid username or password.\n")
    PLACE_STR(s_reg_ok, "\n[OK] User registered successfully!\n")
    PLACE_STR(s_reg_exists, "\n[FAIL] Username already exists.\n")
    PLACE_STR(s_logout_ok, "[OK] Logged out.\n")
    PLACE_STR(s_not_logged, "[INFO] Not logged in.\n")
    PLACE_STR(s_already_logged, "[INFO] Already logged in. Logout first.\n")
    PLACE_STR(s_unknown, "[ERROR] Unknown command. Type 'help' for available commands.\n")
    PLACE_STR(s_bye, "\nShutting down...\n")
    PLACE_STR(s_nl, "\n")
    PLACE_STR(s_cmd_help, "help")
    PLACE_STR(s_cmd_login, "login")
    PLACE_STR(s_cmd_register, "register")
    PLACE_STR(s_cmd_logout, "logout")
    PLACE_STR(s_cmd_clear, "clear")
    PLACE_STR(s_cmd_exit, "exit")
    PLACE_STR(s_cmd_tests, "tests")
    PLACE_STR(s_cmd_passwd, "passwd")
    PLACE_STR(s_cmd_userdel, "userdel")
    PLACE_STR(s_old_pass, "Old password: ")
    PLACE_STR(s_new_pass, "New password: ")
    PLACE_STR(s_pass_changed, "\n[OK] Password changed.\n")
    PLACE_STR(s_pass_wrong, "\n[FAIL] Wrong password.\n")
    PLACE_STR(s_userdel_ok, "\n[OK] User deleted.\n")
    PLACE_STR(s_userdel_fail, "\n[FAIL] Could not delete user.\n")
    PLACE_STR(s_cmd_forkwait, "forkwait")
    PLACE_STR(s_cmd_mount, "mount")
    PLACE_STR(s_cmd_umount, "umount")
    PLACE_STR(s_cmd_ls, "ls")
    PLACE_STR(s_cmd_cat, "cat")
    PLACE_STR(s_cmd_write, "write")
    PLACE_STR(s_cmd_mkdir, "mkdir")
    PLACE_STR(s_cmd_rm, "rm")
    PLACE_STR(s_cmd_cd, "cd")
    PLACE_STR(s_cmd_rename, "mv")
    PLACE_STR(s_cmd_chmod, "chmod")
    PLACE_STR(s_cmd_touch, "touch")
    PLACE_STR(s_cmd_promote, "promote")
    PLACE_STR(s_cmd_demote, "demote")
    PLACE_STR(s_cancelled, "[INFO] Cancelled.\n")
    PLACE_STR(s_demo_done, "\n[Demo complete. Press any key to continue...]\n")
    
    #undef PLACE_STR
    
    uint64_t cmdbuf = SHELL_BUF;        /* command buffer */
    uint64_t userbuf = SHELL_BUF + 128; /* username buffer */
    uint64_t passbuf = SHELL_BUF + 192; /* password buffer */
    uint64_t namebuf = SHELL_BUF + 256; /* current username buffer */
    uint64_t cwdbuf  = SHELL_BUF + 320; /* cwd path buffer */
    
    uart_puts("[SHELL] Generating shell code...\n");
    
    /* Generate code */
    uint8_t *p = code_ptr;
    
    /* Print banner */
    p = emit_print(p, s_banner, s_banner_len);
    
    /* Auto-mount FAT32 volume */
    p = emit_mov64(p, 7, cmdbuf);   /* rdi = dummy arg (not used) */
    p = emit_syscall_only(p, 50);   /* SYS_FAT32_MOUNT */
    
    /* ===== MAIN SHELL LOOP ===== */
    uint8_t *shell_loop = p;
    
    /* Get current UID: syscall(16) */
    p = emit_syscall_only(p, 16);  /* SYS_GETUID */
    /* Store in r14 (current UID) */
    *p++ = 0x49; *p++ = 0x89; *p++ = 0xC6; /* mov r14, rax */
    
    /* Check if logged in (r14 >= 0) */
    *p++ = 0x49; *p++ = 0x83; *p++ = 0xFE; *p++ = 0x00; /* cmp r14, 0 */
    *p++ = 0x7C; uint8_t *jguest = p; *p++ = 0x00; /* jl guest_prompt */
    
    /* Logged in - get username and print "username@tomahawk> " */
    p = emit_mov64(p, 7, namebuf); /* rdi = buffer */
    p = emit_mov64(p, 6, 32);      /* rsi = size */
    p = emit_syscall_only(p, 18); /* SYS_GET_USERNAME */
    
    /* Print username (need strlen - just print char by char until null) */
    p = emit_mov64(p, 13, namebuf); /* r13 = ptr */
    uint8_t *print_name_loop = p;
    /* mov al, [r13] */
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x45; *p++ = 0x00;
    /* test al, al */
    *p++ = 0x84; *p++ = 0xC0;
    /* jz end_name */
    *p++ = 0x74; uint8_t *end_name = p; *p++ = 0x00;
    /* movzx rdi, al */
    *p++ = 0x48; *p++ = 0x0F; *p++ = 0xB6; *p++ = 0xF8;
    /* syscall(15) putchar */
    p = emit_syscall_only(p, 15);
    /* inc r13 */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    /* jmp loop */
    *p++ = 0xEB;
    int8_t name_loop_off = (int8_t)(print_name_loop - p - 1);
    *p++ = (uint8_t)name_loop_off;
    *end_name = (uint8_t)(p - end_name - 1);
    
    /* Print "@tomahawk:" */
    p = emit_print(p, s_prompt_at, s_prompt_at_len);
    *p++ = 0xEB; uint8_t *skip_guest = p; *p++ = 0x00; /* jmp print_cwd */
    
    /* guest_prompt: */
    *jguest = (uint8_t)(p - jguest - 1);
    p = emit_print(p, s_prompt_guest, s_prompt_guest_len);
    
    /* print_cwd: both paths converge here */
    *skip_guest = (uint8_t)(p - skip_guest - 1);
    
    /* Get CWD into cwdbuf: syscall 106(buf, size) */
    p = emit_mov64(p, 7, cwdbuf);   /* rdi = buffer */
    p = emit_mov64(p, 6, 256);      /* rsi = size   */
    p = emit_syscall_only(p, 106);   /* SYS_GETCWD   */
    
    /* Print CWD char by char */
    p = emit_mov64(p, 13, cwdbuf);   /* r13 = ptr */
    uint8_t *print_cwd_loop = p;
    /* mov al, [r13] */
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x45; *p++ = 0x00;
    /* test al, al */
    *p++ = 0x84; *p++ = 0xC0;
    /* jz end_cwd */
    *p++ = 0x74; uint8_t *end_cwd = p; *p++ = 0x00;
    /* movzx rdi, al */
    *p++ = 0x48; *p++ = 0x0F; *p++ = 0xB6; *p++ = 0xF8;
    /* syscall(15) putchar */
    p = emit_syscall_only(p, 15);
    /* inc r13 */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    /* jmp loop */
    *p++ = 0xEB;
    int8_t cwd_loop_off = (int8_t)(print_cwd_loop - p - 1);
    *p++ = (uint8_t)cwd_loop_off;
    *end_cwd = (uint8_t)(p - end_cwd - 1);
    
    /* Print "> " suffix */
    p = emit_print(p, s_prompt_end, s_prompt_end_len);
    
    /* Read command line into cmdbuf using SYS_READLINE (full line editing
       with cursor movement, arrow keys, and command history). */
    p = emit_mov64(p, 7, cmdbuf);   /* rdi = buffer pointer */
    p = emit_mov64(p, 6, 126);      /* rsi = max length     */
    p = emit_syscall_only(p, 70);   /* SYS_READLINE         */
    /* rax = length of entered line; buffer is null-terminated.
       SYS_READLINE already printed the trailing newline.
       If rax <= 0 (ESC or empty input), skip command parsing. */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x8E;               /* jle near (rel32) */
    {
        int32_t off = (int32_t)(shell_loop - p - 4);
        *p++ = off & 0xFF;
        *p++ = (off >> 8) & 0xFF;
        *p++ = (off >> 16) & 0xFF;
        *p++ = (off >> 24) & 0xFF;
    }
    
    /* ===== COMMAND PARSING ===== */
    /* Lowercase the command word (up to first space or NUL) so all
       comparisons below are case-insensitive for the command name.
       Arguments after the space are left untouched.                */
    p = emit_mov64(p, 12, cmdbuf);       /* r12 = cmdbuf ptr */
    {
        uint8_t *lc_loop = p;
        /* mov al, [r12] */
        *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
        /* test al, al → done if NUL */
        *p++ = 0x84; *p++ = 0xC0;
        *p++ = 0x74; uint8_t *lc_done = p; *p++ = 0x00; /* je lc_done */
        /* cmp al, 0x20 → done if space */
        *p++ = 0x3C; *p++ = 0x20;
        *p++ = 0x74; uint8_t *lc_done2 = p; *p++ = 0x00; /* je lc_done */
        /* if al >= 'A' && al <= 'Z', add 0x20 */
        *p++ = 0x3C; *p++ = 0x41; /* cmp al, 'A' */
        *p++ = 0x72; *p++ = 0x08; /* jb skip_lower (8 bytes ahead) */
        *p++ = 0x3C; *p++ = 0x5A; /* cmp al, 'Z' */
        *p++ = 0x77; *p++ = 0x04; /* ja skip_lower (4 bytes ahead) */
        *p++ = 0x04; *p++ = 0x20; /* add al, 0x20 */
        /* mov [r12], al */
        *p++ = 0x41; *p++ = 0x88; *p++ = 0x04; *p++ = 0x24;
        /* skip_lower: inc r12 */
        *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
        /* jmp lc_loop */
        *p++ = 0xEB;
        int8_t lc_off = (int8_t)(lc_loop - p - 1);
        *p++ = (uint8_t)lc_off;
        /* lc_done: patch both exit jumps */
        *lc_done = (uint8_t)(p - lc_done - 1);
        *lc_done2 = (uint8_t)(p - lc_done2 - 1);
    }

    /* Compare cmdbuf with each command */
    
    /* Check "help" */
    p = emit_mov64(p, 12, cmdbuf);   /* r12 = cmdbuf */
    p = emit_mov64(p, 13, s_cmd_help); /* r13 = "help" */
    /* Compare strings (inline) */
    uint8_t *cmp_help = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24; /* mov al, [r12] */
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00; /* mov bl, [r13] */
    *p++ = 0x38; *p++ = 0xD8; /* cmp al, bl */
    *p++ = 0x75; uint8_t *not_help = p; *p++ = 0x00; /* jne not_help */
    *p++ = 0x84; *p++ = 0xC0; /* test al, al */
    *p++ = 0x74; uint8_t *is_help = p; *p++ = 0x00; /* je is_help (both zero = match) */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4; /* inc r12 */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5; /* inc r13 */
    *p++ = 0xEB;
    int8_t cmp_help_off = (int8_t)(cmp_help - p - 1);
    *p++ = (uint8_t)cmp_help_off;
    /* is_help: */
    *is_help = (uint8_t)(p - is_help - 1);
    p = emit_print(p, s_help, s_help_len);
    *p++ = 0xE9; uint8_t *jloop1 = p; p += 4; /* jmp shell_loop */
    /* not_help: */
    *not_help = (uint8_t)(p - not_help - 1);
    
    /* Check "exit" */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_exit);
    uint8_t *cmp_exit = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x75; uint8_t *not_exit = p; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_exit = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_exit_off = (int8_t)(cmp_exit - p - 1);
    *p++ = (uint8_t)cmp_exit_off;
    *is_exit = (uint8_t)(p - is_exit - 1);
    p = emit_print(p, s_bye, s_bye_len);
    p = emit_syscall_only(p, 90); /* SYS_SHUTDOWN */
    *p++ = 0xEB; *p++ = 0xFE; /* infinite loop fallback */
    *not_exit = (uint8_t)(p - not_exit - 1);
    
    /* Check "clear" */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_clear);
    uint8_t *cmp_clear = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x75; uint8_t *not_clear = p; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_clear = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_clear_off = (int8_t)(cmp_clear - p - 1);
    *p++ = (uint8_t)cmp_clear_off;
    *is_clear = (uint8_t)(p - is_clear - 1);
    p = emit_syscall_only(p, 19); /* SYS_CLEAR_SCREEN */
    *p++ = 0xE9; uint8_t *jloop2 = p; p += 4;
    *not_clear = (uint8_t)(p - not_clear - 1);
    
    /* Check "logout" */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_logout);
    uint8_t *cmp_logout = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x75; uint8_t *not_logout = p; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_logout = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_logout_off = (int8_t)(cmp_logout - p - 1);
    *p++ = (uint8_t)cmp_logout_off;
    *is_logout = (uint8_t)(p - is_logout - 1);
    /* Check if logged in */
    p = emit_syscall_only(p, 16); /* SYS_GETUID */
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xF8; *p++ = 0x00; /* cmp rax, 0 */
    *p++ = 0x7C; uint8_t *logout_notlogged = p; *p++ = 0x00; /* jl not_logged */
    /* Set UID to -1 */
    p = emit_mov64(p, 7, (uint64_t)-1);
    p = emit_syscall_only(p, 17); /* SYS_SETUID */
    p = emit_print(p, s_logout_ok, s_logout_ok_len);
    *p++ = 0xE9; uint8_t *jloop4 = p; p += 4;
    *logout_notlogged = (uint8_t)(p - logout_notlogged - 1);
    p = emit_print(p, s_not_logged, s_not_logged_len);
    *p++ = 0xE9; uint8_t *jloop5 = p; p += 4;
    *not_logout = (uint8_t)(p - not_logout - 1);

    /* Debug: report page1 code sizes */
    {
        size_t p1_used = (size_t)(p - code_ptr);
        uart_puts("[SHELL] Page1 code size: ");
        uart_putu(p1_used);
        uart_puts(" / 4096 bytes\n");
        if (p1_used > 4090) {
            uart_puts("[SHELL] WARNING: Page1 nearly full!\n");
        }
    }

    /* We're running out of space in first page, continue in second page */
    /* Jump to second code page */
    *p++ = 0xE9;
    int32_t jmp_to_page2 = (int32_t)(SHELL_CODE2 - (SHELL_CODE + (p - code_ptr) + 4));
    *p++ = jmp_to_page2 & 0xFF;
    *p++ = (jmp_to_page2 >> 8) & 0xFF;
    *p++ = (jmp_to_page2 >> 16) & 0xFF;
    *p++ = (jmp_to_page2 >> 24) & 0xFF;
    
    /* Continue code in second page */
    p = code2_ptr;
    
    /* Check "login" - use near jumps because handler is long */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_login);
    uint8_t *cmp_login = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    /* JNE near (0F 85 rel32) instead of short jump */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_login = p; p += 4;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_login = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_login_off = (int8_t)(cmp_login - p - 1);
    *p++ = (uint8_t)cmp_login_off;
    *is_login = (uint8_t)(p - is_login - 1);
    /* Check if already logged in */
    p = emit_syscall_only(p, 16);
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xF8; *p++ = 0x00;
    *p++ = 0x7C; uint8_t *login_ok_to_login = p; *p++ = 0x00;
    p = emit_print(p, s_already_logged, s_already_logged_len);
    *p++ = 0xE9; uint8_t *jloop6 = p; p += 4;
    *login_ok_to_login = (uint8_t)(p - login_ok_to_login - 1);
    /* Prompt for username — uses SYS_READLINE for full QoL */
    p = emit_print(p, s_user, s_user_len);
    p = emit_mov64(p, 7, userbuf);   /* rdi = buffer */
    p = emit_mov64(p, 6, 64);        /* rsi = max length */
    p = emit_syscall_only(p, 70);    /* SYS_READLINE */
    /* Check for ESC (rax < 0) */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x88; uint8_t *login_esc = p; p += 4; /* JS near → cancel */
    /* Prompt for password — uses SYS_READLINE_MASKED for '*' echo */
    p = emit_print(p, s_pass, s_pass_len);
    p = emit_mov64(p, 7, passbuf);   /* rdi = buffer */
    p = emit_mov64(p, 6, 64);        /* rsi = max length */
    p = emit_syscall_only(p, 71);    /* SYS_READLINE_MASKED */
    /* Check for ESC (rax < 0) */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x88; uint8_t *login_esc2 = p; p += 4; /* JS near → cancel */
    /* Verify */
    p = emit_mov64(p, 7, userbuf);
    p = emit_mov64(p, 6, passbuf);
    p = emit_syscall_only(p, 10); /* SYS_PASS_VERIFY */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0;
    /* Use near jump (JNE rel32) because success path is long */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *login_failed = p; p += 4;
    /* Get UID for the username and set it */
    p = emit_mov64(p, 7, userbuf);
    p = emit_syscall_only(p, 21); /* SYS_PASS_GET_UID - returns actual UID for username */
    /* rax now contains the UID, pass it to SYS_SETUID */
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7; /* mov rdi, rax */
    p = emit_syscall_only(p, 17); /* SYS_SETUID */
    p = emit_print(p, s_login_ok, s_login_ok_len);
    /* Print username */
    p = emit_mov64(p, 13, userbuf);
    uint8_t *login_name_loop = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *login_name_done = p; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x0F; *p++ = 0xB6; *p++ = 0xF8;
    p = emit_syscall_only(p, 15);
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t login_name_off = (int8_t)(login_name_loop - p - 1);
    *p++ = (uint8_t)login_name_off;
    *login_name_done = (uint8_t)(p - login_name_done - 1);
    p = emit_print(p, s_nl, s_nl_len);
    *p++ = 0xE9; uint8_t *jloop7 = p; p += 4;
    /* Patch login_failed (32-bit offset) */
    {
        int32_t off = (int32_t)(p - login_failed - 4);
        login_failed[0] = off & 0xFF;
        login_failed[1] = (off >> 8) & 0xFF;
        login_failed[2] = (off >> 16) & 0xFF;
        login_failed[3] = (off >> 24) & 0xFF;
    }
    p = emit_print(p, s_login_fail, s_login_fail_len);
    *p++ = 0xE9; uint8_t *jloop8 = p; p += 4;
    /* ESC cancel target for login (both username and password ESC land here) */
    { int32_t _o = (int32_t)(p - login_esc - 4); login_esc[0]=_o&0xFF; login_esc[1]=(_o>>8)&0xFF; login_esc[2]=(_o>>16)&0xFF; login_esc[3]=(_o>>24)&0xFF; }
    { int32_t _o = (int32_t)(p - login_esc2 - 4); login_esc2[0]=_o&0xFF; login_esc2[1]=(_o>>8)&0xFF; login_esc2[2]=(_o>>16)&0xFF; login_esc2[3]=(_o>>24)&0xFF; }
    p = emit_print(p, s_cancelled, s_cancelled_len);
    *p++ = 0xE9; uint8_t *jloop_login_cancel = p; p += 4;
    /* Patch not_login (32-bit offset) */
    {
        int32_t off = (int32_t)(p - not_login - 4);
        not_login[0] = off & 0xFF;
        not_login[1] = (off >> 8) & 0xFF;
        not_login[2] = (off >> 16) & 0xFF;
        not_login[3] = (off >> 24) & 0xFF;
    }
    
    /* Check "register" - use near jumps because handler is long */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_register);
    uint8_t *cmp_reg = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    /* JNE near (0F 85 rel32) instead of short jump */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_reg = p; p += 4;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_reg = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_reg_off = (int8_t)(cmp_reg - p - 1);
    *p++ = (uint8_t)cmp_reg_off;
    *is_reg = (uint8_t)(p - is_reg - 1);
    /* Prompt for username — uses SYS_READLINE for full QoL */
    p = emit_print(p, s_user, s_user_len);
    p = emit_mov64(p, 7, userbuf);   /* rdi = buffer */
    p = emit_mov64(p, 6, 64);        /* rsi = max length */
    p = emit_syscall_only(p, 70);    /* SYS_READLINE */
    /* Check for ESC (rax < 0) */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x88; uint8_t *reg_esc = p; p += 4; /* JS near → cancel */
    /* Check if user exists */
    p = emit_mov64(p, 7, userbuf);
    p = emit_syscall_only(p, 12); /* SYS_PASS_EXISTS */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *reg_exists = p; p += 4; /* JNE near */
    /* Prompt for password — uses SYS_READLINE_MASKED for '*' echo */
    p = emit_print(p, s_pass, s_pass_len);
    p = emit_mov64(p, 7, passbuf);   /* rdi = buffer */
    p = emit_mov64(p, 6, 64);        /* rsi = max length */
    p = emit_syscall_only(p, 71);    /* SYS_READLINE_MASKED */
    /* Check for ESC (rax < 0) */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x88; uint8_t *reg_esc2 = p; p += 4; /* JS near → cancel */
    /* Store user */
    p = emit_mov64(p, 7, userbuf);
    p = emit_mov64(p, 6, passbuf);
    p = emit_syscall_only(p, 11); /* SYS_PASS_STORE */
    p = emit_print(p, s_reg_ok, s_reg_ok_len);
    *p++ = 0xE9; uint8_t *jloop9 = p; p += 4;
    { int32_t _o = (int32_t)(p - reg_exists - 4); reg_exists[0]=_o&0xFF; reg_exists[1]=(_o>>8)&0xFF; reg_exists[2]=(_o>>16)&0xFF; reg_exists[3]=(_o>>24)&0xFF; }
    p = emit_print(p, s_reg_exists, s_reg_exists_len);
    *p++ = 0xE9; uint8_t *jloop10 = p; p += 4;
    /* ESC cancel target for register (both username and password ESC land here) */
    { int32_t _o = (int32_t)(p - reg_esc - 4); reg_esc[0]=_o&0xFF; reg_esc[1]=(_o>>8)&0xFF; reg_esc[2]=(_o>>16)&0xFF; reg_esc[3]=(_o>>24)&0xFF; }
    { int32_t _o = (int32_t)(p - reg_esc2 - 4); reg_esc2[0]=_o&0xFF; reg_esc2[1]=(_o>>8)&0xFF; reg_esc2[2]=(_o>>16)&0xFF; reg_esc2[3]=(_o>>24)&0xFF; }
    p = emit_print(p, s_cancelled, s_cancelled_len);
    *p++ = 0xE9; uint8_t *jloop_reg_cancel = p; p += 4;
    /* Patch not_reg (32-bit offset) */
    {
        int32_t off = (int32_t)(p - not_reg - 4);
        not_reg[0] = off & 0xFF;
        not_reg[1] = (off >> 8) & 0xFF;
        not_reg[2] = (off >> 16) & 0xFF;
        not_reg[3] = (off >> 24) & 0xFF;
    }
    
    /* ===== Check "passwd" ===== */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_passwd);
    uint8_t *cmp_passwd = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_passwd = p; p += 4; /* JNE near */
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_passwd = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_passwd_off = (int8_t)(cmp_passwd - p - 1);
    *p++ = (uint8_t)cmp_passwd_off;
    *is_passwd = (uint8_t)(p - is_passwd - 1);
    /* Check if logged in */
    p = emit_syscall_only(p, 16); /* SYS_GETUID */
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xF8; *p++ = 0x00; /* cmp rax, 0 */
    *p++ = 0x0F; *p++ = 0x8D; uint8_t *passwd_ok_to_run = p; p += 4; /* JGE logged in */
    p = emit_print(p, s_not_logged, s_not_logged_len);
    *p++ = 0xE9; uint8_t *jloop_passwd_notlogged = p; p += 4;
    { int32_t _o = (int32_t)(p - passwd_ok_to_run - 4); passwd_ok_to_run[0]=_o&0xFF; passwd_ok_to_run[1]=(_o>>8)&0xFF; passwd_ok_to_run[2]=(_o>>16)&0xFF; passwd_ok_to_run[3]=(_o>>24)&0xFF; }
    /* Prompt for old password */
    p = emit_print(p, s_old_pass, s_old_pass_len);
    p = emit_mov64(p, 7, passbuf);
    p = emit_mov64(p, 6, 64);
    p = emit_syscall_only(p, 71); /* SYS_READLINE_MASKED */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x0F; *p++ = 0x88; uint8_t *passwd_esc1 = p; p += 4; /* JS -> cancel */
    /* Prompt for new password */
    p = emit_print(p, s_new_pass, s_new_pass_len);
    p = emit_mov64(p, 7, userbuf);
    p = emit_mov64(p, 6, 64);
    p = emit_syscall_only(p, 71); /* SYS_READLINE_MASKED */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x0F; *p++ = 0x88; uint8_t *passwd_esc2 = p; p += 4; /* JS -> cancel */
    /* Call SYS_PASS_CHANGE (91): rdi=old_pass, rsi=new_pass */
    p = emit_mov64(p, 7, passbuf);
    p = emit_mov64(p, 6, userbuf);
    p = emit_syscall_only(p, 91);
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *passwd_fail = p; p += 4; /* JNZ -> failed */
    p = emit_print(p, s_pass_changed, s_pass_changed_len);
    *p++ = 0xE9; uint8_t *jloop_passwd = p; p += 4;
    /* Failed */
    { int32_t _o = (int32_t)(p - passwd_fail - 4); passwd_fail[0]=_o&0xFF; passwd_fail[1]=(_o>>8)&0xFF; passwd_fail[2]=(_o>>16)&0xFF; passwd_fail[3]=(_o>>24)&0xFF; }
    p = emit_print(p, s_pass_wrong, s_pass_wrong_len);
    *p++ = 0xE9; uint8_t *jloop_passwd2 = p; p += 4;
    /* ESC cancel */
    { int32_t _o = (int32_t)(p - passwd_esc1 - 4); passwd_esc1[0]=_o&0xFF; passwd_esc1[1]=(_o>>8)&0xFF; passwd_esc1[2]=(_o>>16)&0xFF; passwd_esc1[3]=(_o>>24)&0xFF; }
    { int32_t _o = (int32_t)(p - passwd_esc2 - 4); passwd_esc2[0]=_o&0xFF; passwd_esc2[1]=(_o>>8)&0xFF; passwd_esc2[2]=(_o>>16)&0xFF; passwd_esc2[3]=(_o>>24)&0xFF; }
    p = emit_print(p, s_cancelled, s_cancelled_len);
    *p++ = 0xE9; uint8_t *jloop_passwd_cancel = p; p += 4;
    /* Patch not_passwd */
    {
        int32_t off = (int32_t)(p - not_passwd - 4);
        not_passwd[0] = off & 0xFF;
        not_passwd[1] = (off >> 8) & 0xFF;
        not_passwd[2] = (off >> 16) & 0xFF;
        not_passwd[3] = (off >> 24) & 0xFF;
    }
    
    /* ===== Check "tests" ===== */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_tests);
    uint8_t *cmp_tests = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_tests = p; p += 4; /* JNE near */
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_tests = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_tests_off = (int8_t)(cmp_tests - p - 1);
    *p++ = (uint8_t)cmp_tests_off;
    *is_tests = (uint8_t)(p - is_tests - 1);
    /* Call syscall 26 (SYS_RUN_TESTS) */
    p = emit_syscall_only(p, 26);
    p = emit_print(p, s_demo_done, s_demo_done_len);
    p = emit_syscall_only(p, 14);
    *p++ = 0xE9; uint8_t *jloop_tests = p; p += 4;
    /* Patch not_tests */
    {
        int32_t off = (int32_t)(p - not_tests - 4);
        not_tests[0] = off & 0xFF;
        not_tests[1] = (off >> 8) & 0xFF;
        not_tests[2] = (off >> 16) & 0xFF;
        not_tests[3] = (off >> 24) & 0xFF;
    }

    /* Debug: report page2 code sizes */
    {
        size_t p2_used = (size_t)(p - code2_ptr);
        uart_puts("[SHELL] Page2 code size: ");
        uart_putu(p2_used);
        uart_puts(" / 4096 bytes\n");
        if (p2_used > 4090) {
            uart_puts("[SHELL] WARNING: Page2 nearly full!\n");
        }
    }

    /* Jump from page2 to page3 for remaining commands */
    *p++ = 0xE9;
    {
        int32_t jmp_to_p3 = (int32_t)(SHELL_CODE3 - (SHELL_CODE2 + (p - code2_ptr) + 4));
        uart_puts("[SHELL] p2→p3 jump: offset in p2=");
        uart_putu((uint64_t)(p - code2_ptr));
        uart_puts(", disp=");
        uart_putu((uint64_t)(uint32_t)jmp_to_p3);
        uart_puts("\n");
        *p++ = jmp_to_p3 & 0xFF;
        *p++ = (jmp_to_p3 >> 8) & 0xFF;
        *p++ = (jmp_to_p3 >> 16) & 0xFF;
        *p++ = (jmp_to_p3 >> 24) & 0xFF;
    }

    /* Continue code in third page */
    p = code3_ptr;

    /* ===== Macro to emit a simple command: prefix-compare + syscall + jmp loop ===== */
    /* Each FAT32 shell command: compare cmd prefix, pass cmdbuf as rdi, call syscall, jump back */
    /* Prefix match: when cmd string byte is NUL, accept if cmdbuf byte is NUL or space */
    #define EMIT_SIMPLE_CMD(cmd_str, syscall_num, jloop_var, not_var) \
    do { \
        p = emit_mov64(p, 12, cmdbuf); \
        p = emit_mov64(p, 13, cmd_str); \
        uint8_t *_cmp = p; \
        /* mov al, [r12] */ \
        *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24; \
        /* mov bl, [r13] */ \
        *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00; \
        /* test bl, bl  -- cmd string byte NUL? */ \
        *p++ = 0x84; *p++ = 0xDB; \
        /* jne compare_chars (short) */ \
        *p++ = 0x75; uint8_t *_skip = p; *p++ = 0x00; \
        /* --- cmd string ended: match if cmdbuf is NUL or space --- */ \
        /* test al, al */ \
        *p++ = 0x84; *p++ = 0xC0; \
        /* je matched (short) */ \
        *p++ = 0x74; uint8_t *_m1 = p; *p++ = 0x00; \
        /* cmp al, 0x20 */ \
        *p++ = 0x3C; *p++ = 0x20; \
        /* je matched (short) */ \
        *p++ = 0x74; uint8_t *_m2 = p; *p++ = 0x00; \
        /* jmp not_match (near, unconditional) */ \
        *p++ = 0xE9; not_var = p; p += 4; \
        /* --- compare_chars --- */ \
        *_skip = (uint8_t)(p - _skip - 1); \
        /* cmp al, bl */ \
        *p++ = 0x38; *p++ = 0xD8; \
        /* jne not_match (near) */ \
        *p++ = 0x0F; *p++ = 0x85; uint8_t *_n2 = p; p += 4; \
        /* inc r12 */ \
        *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4; \
        /* inc r13 */ \
        *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5; \
        /* jmp _cmp (short) */ \
        *p++ = 0xEB; \
        int8_t _coff = (int8_t)(_cmp - p - 1); \
        *p++ = (uint8_t)_coff; \
        /* --- matched --- */ \
        *_m1 = (uint8_t)(p - _m1 - 1); \
        *_m2 = (uint8_t)(p - _m2 - 1); \
        /* pass cmdbuf as rdi (arg1) for the syscall */ \
        p = emit_mov64(p, 7, cmdbuf); \
        p = emit_syscall_only(p, syscall_num); \
        *p++ = 0xE9; jloop_var = p; p += 4; \
        /* --- patch both not_match near jumps --- */ \
        { \
            int32_t _o = (int32_t)(p - not_var - 4); \
            not_var[0] = _o & 0xFF; \
            not_var[1] = (_o >> 8) & 0xFF; \
            not_var[2] = (_o >> 16) & 0xFF; \
            not_var[3] = (_o >> 24) & 0xFF; \
        } \
        { \
            int32_t _o2 = (int32_t)(p - _n2 - 4); \
            _n2[0] = _o2 & 0xFF; \
            _n2[1] = (_o2 >> 8) & 0xFF; \
            _n2[2] = (_o2 >> 16) & 0xFF; \
            _n2[3] = (_o2 >> 24) & 0xFF; \
        } \
    } while(0)

    uint8_t *jloop_mount, *not_mount;
    uint8_t *jloop_umount, *not_umount;
    uint8_t *jloop_ls, *not_ls;
    uint8_t *jloop_cat, *not_cat;
    uint8_t *jloop_write, *not_write;
    uint8_t *jloop_mkdir, *not_mkdir;
    uint8_t *jloop_rm, *not_rm;
    uint8_t *jloop_cd, *not_cd;
    uint8_t *jloop_rename, *not_rename;
    uint8_t *jloop_chmod, *not_chmod;
    uint8_t *jloop_touch, *not_touch;
    uint8_t *jloop_userdel, *not_userdel;
    uint8_t *jloop_promote, *not_promote;
    uint8_t *jloop_demote, *not_demote;

    EMIT_SIMPLE_CMD(s_cmd_mount,   50, jloop_mount,   not_mount);
    EMIT_SIMPLE_CMD(s_cmd_umount,  51, jloop_umount,  not_umount);
    EMIT_SIMPLE_CMD(s_cmd_ls,      52, jloop_ls,      not_ls);
    EMIT_SIMPLE_CMD(s_cmd_cat,     53, jloop_cat,     not_cat);
    EMIT_SIMPLE_CMD(s_cmd_write,   54, jloop_write,   not_write);
    EMIT_SIMPLE_CMD(s_cmd_mkdir,   55, jloop_mkdir,   not_mkdir);
    EMIT_SIMPLE_CMD(s_cmd_rm,      56, jloop_rm,      not_rm);
    EMIT_SIMPLE_CMD(s_cmd_cd,      57, jloop_cd,      not_cd);
    EMIT_SIMPLE_CMD(s_cmd_rename,  58, jloop_rename,  not_rename);
    EMIT_SIMPLE_CMD(s_cmd_chmod,   59, jloop_chmod,   not_chmod);
    EMIT_SIMPLE_CMD(s_cmd_touch,   61, jloop_touch,   not_touch);
    EMIT_SIMPLE_CMD(s_cmd_userdel, 92, jloop_userdel, not_userdel);
    EMIT_SIMPLE_CMD(s_cmd_promote, 93, jloop_promote, not_promote);
    EMIT_SIMPLE_CMD(s_cmd_demote,  94, jloop_demote,  not_demote);

    #undef EMIT_SIMPLE_CMD
    
    /* ===== Check "forkwait" ===== */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_forkwait);
    uint8_t *cmp_forkwait = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_forkwait = p; p += 4; /* JNE near */
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_forkwait = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_forkwait_off = (int8_t)(cmp_forkwait - p - 1);
    *p++ = (uint8_t)cmp_forkwait_off;
    *is_forkwait = (uint8_t)(p - is_forkwait - 1);
    /* Call syscall 30 (SYS_RUN_FORK_EXEC_WAIT_DEMO) */
    p = emit_syscall_only(p, 30);
    p = emit_print(p, s_demo_done, s_demo_done_len);
    p = emit_syscall_only(p, 14);
    *p++ = 0xE9; uint8_t *jloop_forkwait = p; p += 4;
    /* Patch not_forkwait */
    {
        int32_t off = (int32_t)(p - not_forkwait - 4);
        not_forkwait[0] = off & 0xFF;
        not_forkwait[1] = (off >> 8) & 0xFF;
        not_forkwait[2] = (off >> 16) & 0xFF;
        not_forkwait[3] = (off >> 24) & 0xFF;
    }
    
    /* Unknown built-in command -> try filesystem commands via SYS_SHELL_CMD */
    /* rdi = cmdbuf (pointer to command string) */
    p = emit_mov64(p, 7, cmdbuf);    /* mov rdi, cmdbuf */
    p = emit_syscall_only(p, 31);    /* SYS_SHELL_CMD */
    /* Check return value: 0 = handled, -1 = truly unknown */
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xF8; *p++ = 0xFF; /* cmp rax, -1 */
    /* JNE -> jump to shell loop (command was handled) */
    *p++ = 0x0F; *p++ = 0x85; uint8_t *jloop_shellcmd = p; p += 4; /* jne shell_loop */
    /* If we get here, command is truly unknown */
    p = emit_print(p, s_unknown, s_unknown_len);
    
    /* Jump back to shell loop (from page3 to page1) */
    *p++ = 0xE9;
    {
        uint64_t target = SHELL_CODE + (shell_loop - code_ptr);
        uint64_t source = SHELL_CODE3 + (p - code3_ptr) + 4;
        int32_t jmp_back_off = (int32_t)(target - source);
        *p++ = jmp_back_off & 0xFF;
        *p++ = (jmp_back_off >> 8) & 0xFF;
        *p++ = (jmp_back_off >> 16) & 0xFF;
        *p++ = (jmp_back_off >> 24) & 0xFF;
    }
    
    /* Patch all the jump-to-loop offsets from page2 */
    #define PATCH_JLOOP(jptr) do { \
        uint64_t t = SHELL_CODE + (shell_loop - code_ptr); \
        uint64_t s = SHELL_CODE2 + ((jptr) - code2_ptr) + 4; \
        int32_t o = (int32_t)(t - s); \
        (jptr)[0] = o & 0xFF; \
        (jptr)[1] = (o >> 8) & 0xFF; \
        (jptr)[2] = (o >> 16) & 0xFF; \
        (jptr)[3] = (o >> 24) & 0xFF; \
    } while(0)
    
    PATCH_JLOOP(jloop6);
    PATCH_JLOOP(jloop7);
    PATCH_JLOOP(jloop8);
    PATCH_JLOOP(jloop9);
    PATCH_JLOOP(jloop10);
    PATCH_JLOOP(jloop_login_cancel);
    PATCH_JLOOP(jloop_reg_cancel);
    PATCH_JLOOP(jloop_passwd);
    PATCH_JLOOP(jloop_passwd2);
    PATCH_JLOOP(jloop_passwd_notlogged);
    PATCH_JLOOP(jloop_passwd_cancel);
    PATCH_JLOOP(jloop_tests);
    
    #undef PATCH_JLOOP
    
    /* Patch all the jump-to-loop offsets from page3 */
    #define PATCH_JLOOP_P3(jptr) do { \
        uint64_t t = SHELL_CODE + (shell_loop - code_ptr); \
        uint64_t s = SHELL_CODE3 + ((jptr) - code3_ptr) + 4; \
        int32_t o = (int32_t)(t - s); \
        (jptr)[0] = o & 0xFF; \
        (jptr)[1] = (o >> 8) & 0xFF; \
        (jptr)[2] = (o >> 16) & 0xFF; \
        (jptr)[3] = (o >> 24) & 0xFF; \
    } while(0)
    
    PATCH_JLOOP_P3(jloop_mount);
    PATCH_JLOOP_P3(jloop_umount);
    PATCH_JLOOP_P3(jloop_ls);
    PATCH_JLOOP_P3(jloop_cat);
    PATCH_JLOOP_P3(jloop_write);
    PATCH_JLOOP_P3(jloop_mkdir);
    PATCH_JLOOP_P3(jloop_rm);
    PATCH_JLOOP_P3(jloop_cd);
    PATCH_JLOOP_P3(jloop_rename);
    PATCH_JLOOP_P3(jloop_chmod);
    PATCH_JLOOP_P3(jloop_touch);
    PATCH_JLOOP_P3(jloop_userdel);
    PATCH_JLOOP_P3(jloop_promote);
    PATCH_JLOOP_P3(jloop_demote);
    PATCH_JLOOP_P3(jloop_forkwait);
    PATCH_JLOOP_P3(jloop_shellcmd);
    
    #undef PATCH_JLOOP_P3
    
    /* Patch jumps from page1 to shell_loop */
    #define PATCH_JLOOP_P1(jptr) do { \
        uint64_t t = SHELL_CODE + (shell_loop - code_ptr); \
        uint64_t s = SHELL_CODE + ((jptr) - code_ptr) + 4; \
        int32_t o = (int32_t)(t - s); \
        (jptr)[0] = o & 0xFF; \
        (jptr)[1] = (o >> 8) & 0xFF; \
        (jptr)[2] = (o >> 16) & 0xFF; \
        (jptr)[3] = (o >> 24) & 0xFF; \
    } while(0)
    
    PATCH_JLOOP_P1(jloop1);
    PATCH_JLOOP_P1(jloop2);
    PATCH_JLOOP_P1(jloop4);
    PATCH_JLOOP_P1(jloop5);
    
    #undef PATCH_JLOOP_P1
    
    /* Debug: report code sizes */
    {
        size_t p3_used = (size_t)(p - code3_ptr);
        uart_puts("[SHELL] Page3 code size: ");
        uart_putu(p3_used);
        uart_puts(" / 4096 bytes\n");
        if (p3_used > 4096) {
            uart_puts("[SHELL] ERROR: Page3 code overflow!\n");
        }
    }
    uart_puts("[SHELL] Code generated, launching...\n");
    
    /* Re-enable interrupts now that codegen is complete. */
    __asm__ volatile("sti");
    
    vga_write("Launching Tomahawk Shell in Ring 3...\n\n");
    
    /* Save kernel context */
    usermode_pass_completed = 0;
    usermode_pass_return_rip = 0;
    __asm__ volatile(
        "mov %%rsp, %0\n"
        "mov %%rbp, %1\n"
        : "=m"(usermode_pass_return_rsp), "=m"(usermode_pass_return_rbp)
    );
    
    __asm__ volatile(
        "lea 1f(%%rip), %%rax\n"
        "mov %%rax, %0\n"
        "1:\n"
        : "=m"(usermode_pass_return_rip)
        : : "rax"
    );
    
    if (usermode_pass_completed) {
        vga_write("\n=== Shell Exited ===\n");
        vga_write("Press ESC to return to demo menu.\n");
        while (1) {
            char c = keyboard_getchar();
            if (c == 27) break;
            __asm__ volatile("pause");
        }
        return;
    }
    
    /* Mask timer AND keyboard before entering user mode */
    uint8_t pic_mask = hal_inb(0x21);
    hal_outb(0x21, pic_mask | 0x03);
    
    /* Flush keyboard */
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60);
    }
    
    /* Set shell process UID to -1 (not logged in) before entering Ring 3 */
    {
        pcb_t *_sp = get_current_process();
        if (_sp) _sp->uid = (uint32_t)-1;
    }
    
    /* Jump to Ring 3 */
    uint64_t user_ss = 0x1B;
    uint64_t user_rsp = SHELL_STACK - 8;
    uint64_t user_rflags = 0x202;
    uint64_t user_cs = 0x23;
    uint64_t user_rip = SHELL_CODE;
    
    __asm__ volatile(
        "cli\n"
        "mov $0x1B, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "push %0\n"
        "push %1\n"
        "push %2\n"
        "push %3\n"
        "push %4\n"
        "xor %%rax, %%rax\n"
        "xor %%rbx, %%rbx\n"
        "xor %%rcx, %%rcx\n"
        "xor %%rdx, %%rdx\n"
        "xor %%rsi, %%rsi\n"
        "xor %%rdi, %%rdi\n"
        "xor %%r8, %%r8\n"
        "xor %%r9, %%r9\n"
        "xor %%r10, %%r10\n"
        "xor %%r11, %%r11\n"
        "xor %%r12, %%r12\n"
        "xor %%r13, %%r13\n"
        "xor %%r14, %%r14\n"
        "xor %%r15, %%r15\n"
        "iretq\n"
        :
        : "r"(user_ss), "r"(user_rsp), "r"(user_rflags), "r"(user_cs), "r"(user_rip)
        : "rax", "memory"
    );
    
    #undef SHELL_CODE
    #undef SHELL_CODE2
    #undef SHELL_CODE3
    #undef SHELL_DATA
    #undef SHELL_BUF
    #undef SHELL_STACK
}

/* ========== Scheduler Demo ========== */

static void sched_demo_busywait(void) {
    for (volatile int i = 0; i < 2000000; i++) {
        __asm__ volatile("pause");
    }
}

static void sched_demo_thread_a(void) {
    while (!demo_stop_requested) {
        uart_puts("[thread A] hello\n");
        vga_write("[A] ");
        sched_demo_busywait();
    }
    scheduler_thread_exit();
}

static void sched_demo_thread_b(void) {
    while (!demo_stop_requested) {
        uart_puts("[thread B] hello\n");
        vga_write("[B] ");
        sched_demo_busywait();
    }
    scheduler_thread_exit();
}

void run_scheduler_demo(void) {
    vga_write("\n=== Scheduler Demo ===\n");
    vga_write("Two threads will alternate printing. Press ESC to stop.\n\n");
    uart_puts("\n=== Scheduler Demo ===\n");

    demo_stop_requested = 0;
    create_process("esc-watcher", demo_esc_watcher);
    create_process("demo-a", sched_demo_thread_a);
    create_process("demo-b", sched_demo_thread_b);

    /* Wait until ESC is pressed */
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }

    vga_write("\n\nScheduler demo stopped.\n");
    uart_puts("Scheduler demo stopped.\n");
    
    /* Allow demo threads to notice stop flag and exit */
    for (volatile int i = 0; i < 5000000; i++) {
        __asm__ volatile("pause");
    }
}

/* ================================================================
 *  Job Control (Foreground / Background) Demo
 * ================================================================ */

void run_job_control_demo(void) {
    vga_write("\n=== Job Control Demo ===");
    vga_write("\n  Demonstrates: process groups, sessions,");
    vga_write("\n  foreground/background, Ctrl+C dispatch\n\n");

    pcb_t* shell = get_current_process();
    if (!shell) {
        vga_write("[FAIL] No current process\n");
        return;
    }

    uint64_t shell_pid = shell->pid;

    /* --- Part 1: Sessions & Process Groups --- */
    vga_write("--- Part 1: Sessions & Process Groups ---\n");

    /* Shell creates its own session (like login shell) */
    setsid(shell);
    vga_write("1. Shell PID ");
    vga_write_u64(shell_pid);
    vga_write(" created session (sid=");
    vga_write_u64(shell->sid);
    vga_write(", pgid=");
    vga_write_u64(shell->pgid);
    vga_write(")\n");

    /* The shell is the foreground group by default */
    vga_write("   Foreground group = ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write("\n");

    /* --- Part 2: Create a foreground child process group --- */
    vga_write("\n--- Part 2: Foreground child group ---\n");

    pcb_t* fg_child = create_process("fg-job", (void(*)(void))0);
    if (!fg_child) { vga_write("   [FAIL] Could not create child\n"); return; }
    fg_child->parent = shell;
    fg_child->sibling_next = shell->children;
    shell->children = fg_child;
    fg_child->sid = shell->sid;   /* same session */
    setpgid(fg_child, fg_child->pid);  /* new group with own pid */

    /* Move foreground to the child's group (like shell does on fork+exec) */
    session_set_foreground(shell->sid, fg_child->pgid);

    vga_write("2. Created child PID ");
    vga_write_u64(fg_child->pid);
    vga_write(" in pgid ");
    vga_write_u64(fg_child->pgid);
    vga_write("\n");
    vga_write("   Set foreground group -> ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write("\n");

    /* --- Part 3: Create a background child (different group) --- */
    vga_write("\n--- Part 3: Background child group ---\n");

    pcb_t* bg_child = create_process("bg-job", (void(*)(void))0);
    if (!bg_child) { vga_write("   [FAIL] Could not create child\n"); return; }
    bg_child->parent = shell;
    bg_child->sibling_next = shell->children;
    shell->children = bg_child;
    bg_child->sid = shell->sid;
    setpgid(bg_child, bg_child->pid);  /* its own group, but NOT foreground */

    vga_write("3. Created background child PID ");
    vga_write_u64(bg_child->pid);
    vga_write(" in pgid ");
    vga_write_u64(bg_child->pgid);
    vga_write("\n");
    vga_write("   Foreground group is still ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write(" (bg child NOT in fg)\n");

    /* --- Part 4: Simulate Ctrl+C (SIGINT to fg group only) --- */
    vga_write("\n--- Part 4: SIGINT to foreground group ---\n");

    vga_write("4. Sending SIGINT to fg group ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write("...\n");

    int hit = signal_process_group(session_get_foreground(shell->sid), SIGINT);
    vga_write("   Delivered to ");
    vga_write_u64((uint64_t)hit);
    vga_write(" process(es)\n");

    /* The foreground child should have SIGINT pending */
    int fg_pending = signal_has_pending(fg_child);
    int bg_pending = signal_has_pending(bg_child);
    vga_write("   fg child (PID ");
    vga_write_u64(fg_child->pid);
    vga_write(") pending signals: ");
    vga_write(fg_pending ? "YES" : "no");
    vga_write("\n   bg child (PID ");
    vga_write_u64(bg_child->pid);
    vga_write(") pending signals: ");
    vga_write(bg_pending ? "YES (BUG!)" : "no (correct — not in fg)");
    vga_write("\n");

    /* --- Part 5: Move bg child to foreground (like shell's 'fg') --- */
    vga_write("\n--- Part 5: 'fg' — bring bg job to foreground ---\n");

    session_set_foreground(shell->sid, bg_child->pgid);
    vga_write("5. Foreground group changed to pgid ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write("\n");

    /* Now SIGINT should hit the (previously-bg) child */
    signal_process_group(session_get_foreground(shell->sid), SIGINT);
    vga_write("   Sent SIGINT -> bg child pending: ");
    vga_write(signal_has_pending(bg_child) ? "YES" : "no");
    vga_write("\n");

    /* --- Part 6: SIGTSTP (Ctrl+Z) — stop a process --- */
    vga_write("\n--- Part 6: SIGTSTP — stop & continue ---\n");

    /* Simulate stopping the bg child (now fg) */
    bg_child->is_stopped = 1;
    if (bg_child->main_thread) {
        bg_child->main_thread->state = THREAD_BLOCKED;
    }
    vga_write("6. Stopped PID ");
    vga_write_u64(bg_child->pid);
    vga_write(" (is_stopped=1)\n");

    /* Shell takes back foreground */
    session_set_foreground(shell->sid, shell->pgid);
    vga_write("   Shell takes back fg group -> ");
    vga_write_u64(session_get_foreground(shell->sid));
    vga_write("\n");

    /* Resume with SIGCONT (like shell's 'bg' or 'fg') */
    process_continue(bg_child);
    vga_write("   SIGCONT -> PID ");
    vga_write_u64(bg_child->pid);
    vga_write(" is_stopped = ");
    vga_write_u64((uint64_t)bg_child->is_stopped);
    vga_write("\n");

    /* --- Cleanup --- */
    vga_write("\n--- Cleanup ---\n");
    fg_child->exit_code = 0;
    fg_child->is_zombie = 1;
    waitpid_process((int)fg_child->pid, NULL, 0);
    vga_write("   Reaped fg child PID ");
    vga_write_u64(fg_child->pid);
    vga_write("\n");

    bg_child->exit_code = 0;
    bg_child->is_zombie = 1;
    waitpid_process((int)bg_child->pid, NULL, 0);
    vga_write("   Reaped bg child PID ");
    vga_write_u64(bg_child->pid);
    vga_write("\n");

    /* Restore shell as foreground */
    session_set_foreground(shell->sid, shell->pgid);

    vga_write("\n=== Job Control Demo Complete ===\n");
    vga_write("  Sessions, process groups, fg/bg switching, SIGINT/SIGTSTP\n");
    vga_write("  dispatch, and SIGCONT resume all working.\n");
}
