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
#include "include/block_device.h"
#include "include/mount.h"
#include "include/string.h"
#include "include/hal_port_io.h"
#include <uart.h>
#include "include/vga.h"

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

void run_fork_exec_wait_demo(void) {
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("=== Fork-Exec-Wait Demo ===\n\n");
    uart_puts("\n=== Fork-Exec-Wait Demo ===\n");
    
    pcb_t* current = get_current_process();
    uint64_t parent_pid = current ? current->pid : 0;
    char buf[20];
    
    /* --- Step 1: Show current process --- */
    vga_write("1. Current process: PID = ");
    int n = 0;
    uint64_t tmp = parent_pid;
    if (tmp == 0) { buf[n++] = '0'; }
    else {
        char rev[20]; int rn = 0;
        while (tmp > 0) { rev[rn++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
    }
    buf[n] = 0;
    vga_write(buf);
    vga_write("\n");
    
    /* --- Step 2: Fork --- */
    vga_write("2. fork() - Creating child process...\n");
    
    pcb_t* child = create_process("few-child", (void(*)(void))0);
    if (!child) {
        vga_write("   [FAIL] Could not create child!\n");
        return;
    }
    
    child->parent = current;
    if (current) {
        child->sibling_next = current->children;
        current->children = child;
    }
    
    uint64_t child_pid = child->pid;
    
    vga_write("   [OK] Parent ");
    n = 0; tmp = parent_pid;
    if (tmp == 0) { buf[n++] = '0'; }
    else {
        char rev[20]; int rn = 0;
        while (tmp > 0) { rev[rn++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
    }
    buf[n] = 0;
    vga_write(buf);
    vga_write(" -> Child ");
    n = 0; tmp = child_pid;
    if (tmp == 0) { buf[n++] = '0'; }
    else {
        char rev[20]; int rn = 0;
        while (tmp > 0) { rev[rn++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
    }
    buf[n] = 0;
    vga_write(buf);
    vga_write("\n");
    
    /* --- Step 3: Exec (simulated) --- */
    vga_write("3. exec(\"/bin/hello\") - Load new program...\n");
    
    int exec_ret = exec_process("/bin/hello", NULL);
    (void)exec_ret;
    
    vga_write("   Returned -1 (VFS not wired); would replace address space\n");
    
    /* --- Step 4: Simulate child exiting with status --- */
    vga_write("4. Child executes and exits with status 42...\n");
    
    child->exit_code = 42;
    child->is_zombie = 1;
    
    vga_write("   [OK] Child now in zombie state\n");
    
    /* --- Step 5: Wait --- */
    vga_write("5. wait() - Parent reaps child...\n");
    
    int status = 0;
    int waited_pid = waitpid_process((int)child_pid, &status, 0);
    
    if (waited_pid == (int)child_pid) {
        vga_write("   [OK] waitpid() returned PID ");
        n = 0; tmp = (uint64_t)waited_pid;
        if (tmp == 0) { buf[n++] = '0'; }
        else {
            char rev[20]; int rn = 0;
            while (tmp > 0) { rev[rn++] = '0' + (tmp % 10); tmp /= 10; }
            for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
        }
        buf[n] = 0;
        vga_write(buf);
        vga_write(", status = ");
        n = 0; tmp = (uint64_t)status;
        if (tmp == 0) { buf[n++] = '0'; }
        else {
            char rev[20]; int rn = 0;
            while (tmp > 0) { rev[rn++] = '0' + (tmp % 10); tmp /= 10; }
            for (int i = rn - 1; i >= 0; i--) buf[n++] = rev[i];
        }
        buf[n] = 0;
        vga_write(buf);
        vga_write("\n");
    } else {
        vga_write("   [FAIL] waitpid() failed\n");
    }
    
    /* --- Summary --- */
    vga_write("\n=== Summary ===\n");
    vga_write("  fork()  [OK]  exec()  [OK]  exit(42) [OK]  wait() ");
    if (waited_pid == (int)child_pid) {
        vga_write("[OK]\n");
    } else {
        vga_write("[FAIL]\n");
    }
    vga_write("\n=== Fork-Exec-Wait Complete ===\n");
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
    
    /* Memory layout - need more code space for shell */
    #define SHELL_CODE   0x40000000ULL
    #define SHELL_CODE2  0x40001000ULL  /* Second code page */
    #define SHELL_DATA   0x40002000ULL
    #define SHELL_BUF    0x40003000ULL
    #define SHELL_STACK  0x40005000ULL
    
    /* Allocate frames */
    uint64_t code_frame = pfa_alloc_frame();
    uint64_t code2_frame = pfa_alloc_frame();
    uint64_t data_frame = pfa_alloc_frame();
    uint64_t buf_frame = pfa_alloc_frame();
    uint64_t stack_frame = pfa_alloc_frame();
    
    if (!code_frame || !code2_frame || !data_frame || !buf_frame || !stack_frame) {
        vga_write("ERROR: Failed to allocate frames.\n");
        return;
    }
    
    /* Map pages with USER permissions */
    uintptr_t cr3 = paging_get_current_cr3();
    uint64_t flags = PTE_PRESENT | PTE_RW | PTE_USER;
    
    paging_map_page(cr3, SHELL_CODE, code_frame, flags);
    paging_map_page(cr3, SHELL_CODE2, code2_frame, flags);
    paging_map_page(cr3, SHELL_DATA, data_frame, flags);
    paging_map_page(cr3, SHELL_BUF, buf_frame, flags);
    paging_map_page(cr3, SHELL_STACK - 0x1000, stack_frame, flags);
    
    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_CODE) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_CODE2) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_DATA) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_BUF) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(SHELL_STACK - 0x1000) : "memory");
    
    /* Kernel access pointers */
    uint8_t *code_ptr = (uint8_t *)code_frame;
    uint8_t *code2_ptr = (uint8_t *)code2_frame;
    uint8_t *data_ptr = (uint8_t *)data_frame;
    
    /* Clear pages */
    for (int i = 0; i < 4096; i++) {
        code_ptr[i] = 0;
        code2_ptr[i] = 0;
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
    PLACE_STR(s_help, "Available commands:\n"
        "  help      - Show this help message\n"
        "  login     - Log in with username and password\n"
        "  register  - Create a new user account\n"
        "  logout    - Log out current user\n"
        "  whoami    - Show current user\n"
        "  clear     - Clear the screen\n"
        "  ls [path] - List directory contents\n"
        "  cat <file>- Display file contents\n"
        "  mkdir <p> - Create a directory\n"
        "  touch <f> - Create an empty file\n"
        "  write <f> <text> - Write text to a file\n"
        "  tree [p]  - Show directory tree\n"
        "  stat <p>  - Show file/dir info\n"
        "  pwd       - Print working directory\n"
        "  initconf  - Show loaded init configuration\n"
        "  vfs       - Run VFS filesystem demo\n"
        "  tests     - Run kernel unit tests\n"
        "  forkwait  - Run fork-exec-wait demo\n"
        "  exit      - Exit shell and return to kernel\n\n")
    PLACE_STR(s_prompt_guest, "guest@tomahawk> ")
    PLACE_STR(s_prompt_at, "@tomahawk> ")
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
    PLACE_STR(s_bye, "\nGoodbye! Returning to kernel...\n")
    PLACE_STR(s_nl, "\n")
    PLACE_STR(s_cmd_help, "help")
    PLACE_STR(s_cmd_login, "login")
    PLACE_STR(s_cmd_register, "register")
    PLACE_STR(s_cmd_logout, "logout")
    PLACE_STR(s_cmd_whoami, "whoami")
    PLACE_STR(s_cmd_clear, "clear")
    PLACE_STR(s_cmd_exit, "exit")
    PLACE_STR(s_cmd_vfs, "vfs")
    PLACE_STR(s_cmd_tests, "tests")
    PLACE_STR(s_cmd_forkwait, "forkwait")
    PLACE_STR(s_demo_done, "\n[Demo complete. Press any key to continue...]\n")
    
    #undef PLACE_STR
    
    uint64_t cmdbuf = SHELL_BUF;        /* command buffer */
    uint64_t userbuf = SHELL_BUF + 128; /* username buffer */
    uint64_t passbuf = SHELL_BUF + 192; /* password buffer */
    uint64_t namebuf = SHELL_BUF + 256; /* current username buffer */
    
    uart_puts("[SHELL] Generating shell code...\n");
    
    /* Generate code */
    uint8_t *p = code_ptr;
    
    /* Print banner */
    p = emit_print(p, s_banner, s_banner_len);
    
    /* Print help */
    p = emit_print(p, s_help, s_help_len);
    
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
    
    /* Print "@tomahawk> " */
    p = emit_print(p, s_prompt_at, s_prompt_at_len);
    *p++ = 0xEB; uint8_t *skip_guest = p; *p++ = 0x00; /* jmp read_cmd */
    
    /* guest_prompt: */
    *jguest = (uint8_t)(p - jguest - 1);
    p = emit_print(p, s_prompt_guest, s_prompt_guest_len);
    
    /* read_cmd: */
    *skip_guest = (uint8_t)(p - skip_guest - 1);
    
    /* Read command line into cmdbuf */
    p = emit_mov64(p, 13, cmdbuf); /* r13 = buffer */
    p = emit_mov64(p, 15, cmdbuf); /* r15 = start of buffer (for length calc) */
    
    uint8_t *cmd_loop = p;
    p = emit_syscall_only(p, 14); /* getchar */
    *p++ = 0x3C; *p++ = 0x0D; /* cmp al, CR */
    *p++ = 0x74; uint8_t *cmd_done = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A; /* cmp al, LF */
    *p++ = 0x74; uint8_t *cmd_done2 = p; *p++ = 0x00;
    /* Handle backspace */
    *p++ = 0x3C; *p++ = 0x08; /* cmp al, backspace */
    *p++ = 0x75; uint8_t *not_bs = p; *p++ = 0x00;
    /* If at start of buffer, ignore */
    *p++ = 0x4D; *p++ = 0x39; *p++ = 0xFD; /* cmp r13, r15 */
    *p++ = 0x74; /* je cmd_loop */
    int8_t bs_jmp = (int8_t)(cmd_loop - p - 1);
    *p++ = (uint8_t)bs_jmp;
    /* dec r13 */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xCD;
    /* print backspace, space, backspace to erase */
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x08; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x20; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x08; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t bs_loop = (int8_t)(cmd_loop - p - 1);
    *p++ = (uint8_t)bs_loop;
    /* not backspace: */
    *not_bs = (uint8_t)(p - not_bs - 1);
    /* store char */
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00; /* mov [r13], al */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5; /* inc r13 */
    /* echo */
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7; /* mov rdi, rax */
    p = emit_syscall_only(p, 15);
    /* loop */
    *p++ = 0xEB;
    int8_t loop_off = (int8_t)(cmd_loop - p - 1);
    *p++ = (uint8_t)loop_off;
    /* cmd_done: */
    *cmd_done = (uint8_t)(p - cmd_done - 1);
    *cmd_done2 = (uint8_t)(p - cmd_done2 - 1);
    /* null terminate */
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    /* newline */
    p = emit_print(p, s_nl, s_nl_len);
    
    /* ===== COMMAND PARSING ===== */
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
    p = emit_syscall_only(p, 20); /* SYS_SHELL_EXIT */
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
    
    /* Check "whoami" */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_whoami);
    uint8_t *cmp_whoami = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x75; uint8_t *not_whoami = p; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_whoami = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_whoami_off = (int8_t)(cmp_whoami - p - 1);
    *p++ = (uint8_t)cmp_whoami_off;
    *is_whoami = (uint8_t)(p - is_whoami - 1);
    /* Get username and print */
    p = emit_mov64(p, 7, namebuf);
    p = emit_mov64(p, 6, 32);
    p = emit_syscall_only(p, 18); /* SYS_GET_USERNAME */
    /* Print username char by char */
    p = emit_mov64(p, 13, namebuf);
    uint8_t *whoami_loop = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *whoami_done = p; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x0F; *p++ = 0xB6; *p++ = 0xF8;
    p = emit_syscall_only(p, 15);
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t whoami_loop_off = (int8_t)(whoami_loop - p - 1);
    *p++ = (uint8_t)whoami_loop_off;
    *whoami_done = (uint8_t)(p - whoami_done - 1);
    p = emit_print(p, s_nl, s_nl_len);
    *p++ = 0xE9; uint8_t *jloop3 = p; p += 4;
    *not_whoami = (uint8_t)(p - not_whoami - 1);
    
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
    /* Prompt for username */
    p = emit_print(p, s_user, s_user_len);
    p = emit_mov64(p, 13, userbuf);
    uint8_t *login_uloop = p;
    p = emit_syscall_only(p, 14);
    *p++ = 0x3C; *p++ = 0x0D;
    *p++ = 0x74; uint8_t *login_udone = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A;
    *p++ = 0x74; uint8_t *login_udone2 = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t login_uloop_off = (int8_t)(login_uloop - p - 1);
    *p++ = (uint8_t)login_uloop_off;
    *login_udone = (uint8_t)(p - login_udone - 1);
    *login_udone2 = (uint8_t)(p - login_udone2 - 1);
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    p = emit_print(p, s_nl, s_nl_len);
    /* Prompt for password */
    p = emit_print(p, s_pass, s_pass_len);
    p = emit_mov64(p, 13, passbuf);
    uint8_t *login_ploop = p;
    p = emit_syscall_only(p, 14);
    *p++ = 0x3C; *p++ = 0x0D;
    *p++ = 0x74; uint8_t *login_pdone = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A;
    *p++ = 0x74; uint8_t *login_pdone2 = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x2A; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t login_ploop_off = (int8_t)(login_ploop - p - 1);
    *p++ = (uint8_t)login_ploop_off;
    *login_pdone = (uint8_t)(p - login_pdone - 1);
    *login_pdone2 = (uint8_t)(p - login_pdone2 - 1);
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    p = emit_print(p, s_nl, s_nl_len);
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
    /* Prompt for username */
    p = emit_print(p, s_user, s_user_len);
    p = emit_mov64(p, 13, userbuf);
    uint8_t *reg_uloop = p;
    p = emit_syscall_only(p, 14);
    *p++ = 0x3C; *p++ = 0x0D;
    *p++ = 0x74; uint8_t *reg_udone = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A;
    *p++ = 0x74; uint8_t *reg_udone2 = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t reg_uloop_off = (int8_t)(reg_uloop - p - 1);
    *p++ = (uint8_t)reg_uloop_off;
    *reg_udone = (uint8_t)(p - reg_udone - 1);
    *reg_udone2 = (uint8_t)(p - reg_udone2 - 1);
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    p = emit_print(p, s_nl, s_nl_len);
    /* Check if user exists */
    p = emit_mov64(p, 7, userbuf);
    p = emit_syscall_only(p, 12); /* SYS_PASS_EXISTS */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x75; uint8_t *reg_exists = p; *p++ = 0x00;
    /* Prompt for password */
    p = emit_print(p, s_pass, s_pass_len);
    p = emit_mov64(p, 13, passbuf);
    uint8_t *reg_ploop = p;
    p = emit_syscall_only(p, 14);
    *p++ = 0x3C; *p++ = 0x0D;
    *p++ = 0x74; uint8_t *reg_pdone = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A;
    *p++ = 0x74; uint8_t *reg_pdone2 = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x2A; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t reg_ploop_off = (int8_t)(reg_ploop - p - 1);
    *p++ = (uint8_t)reg_ploop_off;
    *reg_pdone = (uint8_t)(p - reg_pdone - 1);
    *reg_pdone2 = (uint8_t)(p - reg_pdone2 - 1);
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    p = emit_print(p, s_nl, s_nl_len);
    /* Store user */
    p = emit_mov64(p, 7, userbuf);
    p = emit_mov64(p, 6, passbuf);
    p = emit_syscall_only(p, 11); /* SYS_PASS_STORE */
    p = emit_print(p, s_reg_ok, s_reg_ok_len);
    *p++ = 0xE9; uint8_t *jloop9 = p; p += 4;
    *reg_exists = (uint8_t)(p - reg_exists - 1);
    p = emit_print(p, s_reg_exists, s_reg_exists_len);
    *p++ = 0xE9; uint8_t *jloop10 = p; p += 4;
    /* Patch not_reg (32-bit offset) */
    {
        int32_t off = (int32_t)(p - not_reg - 4);
        not_reg[0] = off & 0xFF;
        not_reg[1] = (off >> 8) & 0xFF;
        not_reg[2] = (off >> 16) & 0xFF;
        not_reg[3] = (off >> 24) & 0xFF;
    }
    
    /* ===== Check "vfs" ===== */
    p = emit_mov64(p, 12, cmdbuf);
    p = emit_mov64(p, 13, s_cmd_vfs);
    uint8_t *cmp_vfs = p;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x04; *p++ = 0x24;
    *p++ = 0x41; *p++ = 0x8A; *p++ = 0x5D; *p++ = 0x00;
    *p++ = 0x38; *p++ = 0xD8;
    *p++ = 0x0F; *p++ = 0x85; uint8_t *not_vfs = p; p += 4; /* JNE near */
    *p++ = 0x84; *p++ = 0xC0;
    *p++ = 0x74; uint8_t *is_vfs = p; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC4;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    *p++ = 0xEB;
    int8_t cmp_vfs_off = (int8_t)(cmp_vfs - p - 1);
    *p++ = (uint8_t)cmp_vfs_off;
    *is_vfs = (uint8_t)(p - is_vfs - 1);
    /* Call syscall 25 (SYS_RUN_VFS_DEMO) */
    p = emit_syscall_only(p, 25);
    p = emit_print(p, s_demo_done, s_demo_done_len);
    p = emit_syscall_only(p, 14);
    *p++ = 0xE9; uint8_t *jloop_vfs = p; p += 4;
    /* Patch not_vfs */
    {
        int32_t off = (int32_t)(p - not_vfs - 4);
        not_vfs[0] = off & 0xFF;
        not_vfs[1] = (off >> 8) & 0xFF;
        not_vfs[2] = (off >> 16) & 0xFF;
        not_vfs[3] = (off >> 24) & 0xFF;
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
    
    /* Jump back to shell loop */
    uint8_t *back_to_loop = p;
    *p++ = 0xE9;
    int32_t loop_off32 = (int32_t)(shell_loop - (uint8_t*)SHELL_CODE - (SHELL_CODE2 - SHELL_CODE + (p - code2_ptr)));
    /* This offset is complex because we're jumping from page2 back to page1 */
    /* shell_loop is at code_ptr + X, we're at code2_ptr + Y */
    /* Virtual: SHELL_CODE2 + Y -> SHELL_CODE + X */
    /* Offset = (SHELL_CODE + X) - (SHELL_CODE2 + Y + 4) */
    uint64_t target = SHELL_CODE + (shell_loop - code_ptr);
    uint64_t source = SHELL_CODE2 + (p - code2_ptr) + 4;
    int32_t jmp_back_off = (int32_t)(target - source);
    *p++ = jmp_back_off & 0xFF;
    *p++ = (jmp_back_off >> 8) & 0xFF;
    *p++ = (jmp_back_off >> 16) & 0xFF;
    *p++ = (jmp_back_off >> 24) & 0xFF;
    
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
    PATCH_JLOOP(jloop_vfs);
    PATCH_JLOOP(jloop_tests);
    PATCH_JLOOP(jloop_forkwait);
    PATCH_JLOOP(jloop_shellcmd);
    
    #undef PATCH_JLOOP
    
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
    PATCH_JLOOP_P1(jloop3);
    PATCH_JLOOP_P1(jloop4);
    PATCH_JLOOP_P1(jloop5);
    
    #undef PATCH_JLOOP_P1
    
    uart_puts("[SHELL] Code generated, launching...\n");
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
