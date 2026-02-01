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
#include "include/string.h"
#include <uart.h>
#include "include/vga.h"

extern volatile int demo_stop_requested;
extern void demo_esc_watcher(void);

/* ========== COW Fork Demo ========== */

static volatile int cow_shared_counter = 0;
static volatile int cow_demo_done = 0;

static void cow_parent_thread(void) {
    /* Only run once */
    if (cow_demo_done) {
        scheduler_thread_exit();
        return;
    }
    cow_demo_done = 1;
    
    cow_shared_counter = 100;
    
    vga_write("[Parent] Forking...\n");
    
    /* Fork returns PID of child to parent, 0 to child */
    int child_pid = fork_process();
    
    if (child_pid < 0) {
        uart_puts("[COW Parent] ERROR: fork failed!\n");
        vga_write("[Parent] ERROR: fork failed!\n");
        scheduler_thread_exit();
        return;
    }
    
    if (child_pid == 0) {
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
    } else {
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
    }
    
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
    vga_write("=== VFS with Block Device Layer Demo ===\n\n");
    
    /* Step 1: Initialize block device subsystem */
    vga_write("1. Init block device subsystem... ");
    block_device_init();
    vga_write("[OK]\n");
    
    /* Step 2: Create a RAM block device as VFS backend */
    vga_write("2. Create RAM block device 'vfs_storage'... ");
    struct block_device *storage = ramblock_create("vfs_storage", 32);
    if (!storage) {
        vga_write("[ERROR]\n");
        return;
    }
    block_device_register(storage);
    vga_write("[OK]\n");
    
    /* Step 3: Initialize VFS */
    vga_write("3. Init VFS layer... ");
    vfs_init();
    vga_write("[OK]\n");
    
    /* Step 4: Create a file and write data */
    vga_write("4. Create file, write data... ");
    struct vnode* file = vfs_create_vnode(VREG);
    if (!file) {
        vga_write("[ERROR]\n");
        return;
    }
    
    vfs_open(file);
    const char* test_data = "Hello from VFS over Block Device!";
    vfs_write(file, test_data, 34);
    block_write(storage, 0, test_data, 34);
    vga_write("[OK]\n");
    vga_write("   Data: \"");
    vga_write(test_data);
    vga_write("\"\n");
    
    /* Step 5: Read data back */
    vga_write("5. Read data back...\n");
    char vfs_buffer[64] = {0};
    vfs_read(file, vfs_buffer, 64);
    vga_write("   VFS:   \"");
    vga_write(vfs_buffer);
    vga_write("\"\n");
    
    char blk_buffer[64] = {0};
    block_read(storage, 0, blk_buffer, 64);
    vga_write("   Block: \"");
    vga_write(blk_buffer);
    vga_write("\"\n");
    
    /* Step 6: Show buffer cache statistics */
    vga_write("6. Buffer stats: ");
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
    
    /* Step 7: Cleanup */
    vga_write("7. Sync and cleanup... ");
    buffer_sync_all(storage);
    vfs_close(file);
    block_device_unregister(storage);
    ramblock_destroy(storage);
    vga_write("[OK]\n\n");
    
    vga_write("=== Demo Complete! ===\n");
    vga_write("Block layer: buffer cache + easy swap to disk\n\n");
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

void run_usermode_password_demo(void) {
    vga_write("=== User Mode Password Demo (Ring 3) ===\n\n");
    vga_write("This demo runs a password program in Ring 3 user mode.\n");
    vga_write("It uses syscalls for I/O and password operations.\n\n");
    
    /* Memory layout */
    #define UPASS_CODE   0x40000000ULL
    #define UPASS_DATA   0x40001000ULL
    #define UPASS_BUF    0x40002000ULL
    #define UPASS_STACK  0x40004000ULL
    
    /* Allocate frames */
    uint64_t code_frame = pfa_alloc_frame();
    uint64_t data_frame = pfa_alloc_frame();
    uint64_t buf_frame = pfa_alloc_frame();
    uint64_t stack_frame = pfa_alloc_frame();
    
    if (!code_frame || !data_frame || !buf_frame || !stack_frame) {
        vga_write("ERROR: Failed to allocate frames.\n");
        return;
    }
    
    /* Map pages with USER permissions */
    uintptr_t cr3 = paging_get_current_cr3();
    uint64_t flags = PTE_PRESENT | PTE_RW | PTE_USER;
    
    paging_map_page(cr3, UPASS_CODE, code_frame, flags);
    paging_map_page(cr3, UPASS_DATA, data_frame, flags);
    paging_map_page(cr3, UPASS_BUF, buf_frame, flags);
    paging_map_page(cr3, UPASS_STACK - 0x1000, stack_frame, flags);
    
    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" :: "r"(UPASS_CODE) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(UPASS_DATA) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(UPASS_BUF) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(UPASS_STACK - 0x1000) : "memory");
    
    /* Kernel access pointers (physical = virtual for low addresses in identity map) */
    uint8_t *code_ptr = (uint8_t *)code_frame;
    uint8_t *data_ptr = (uint8_t *)data_frame;
    
    /* Clear pages */
    for (int i = 0; i < 4096; i++) {
        code_ptr[i] = 0;
        data_ptr[i] = 0;
    }
    
    /* Setup strings in data page */
    size_t off = 0;
    
    /* Helper macro to place string at offset and track location */
    #define PLACE_STR(name, text) \
        uint64_t name = UPASS_DATA + off; \
        size_t name##_len = sizeof(text) - 1; \
        { const char *s = text; for (size_t _i = 0; s[_i]; _i++) data_ptr[off++] = s[_i]; data_ptr[off++] = 0; }

    PLACE_STR(s_menu, "\n=== Password Menu ===\n1. Login\n2. Register\n3. Exit\nChoice: ")
    PLACE_STR(s_user, "Username: ")
    PLACE_STR(s_pass, "Password: ")
    PLACE_STR(s_ok, "\n[OK] Login successful!\n")
    PLACE_STR(s_fail, "\n[FAIL] Invalid credentials.\n")
    PLACE_STR(s_reg, "\n[OK] User registered!\n")
    PLACE_STR(s_exists, "\n[FAIL] User already exists.\n")
    PLACE_STR(s_bye, "\nReturning to kernel...\n")
    PLACE_STR(s_nl, "\n")
    
    #undef PLACE_STR
    
    /* Debug: show frame addresses and virtual addresses */
    uart_puts("[DEMO] data_frame=0x");
    uart_puthex(data_frame);
    uart_puts(" s_menu=0x");
    uart_puthex(s_menu);
    uart_puts(" s_menu_len=");
    uart_putu(s_menu_len);
    uart_puts("\n");
    
    /* Debug: Verify data was written - read first few bytes directly from frame */
    uart_puts("[DEMO] Data in frame: ");
    for (int i = 0; i < 10; i++) {
        uart_puthex(data_ptr[i]);
        uart_puts(" ");
    }
    uart_puts("\n");
    
    uint64_t ubuf = UPASS_BUF;        /* username buffer */
    uint64_t pbuf = UPASS_BUF + 64;   /* password buffer */
    
    vga_write("Building user mode program...\n");
    
    /* Generate code */
    uint8_t *p = code_ptr;
    
    /* Print menu */
    p = emit_print(p, s_menu, s_menu_len);
    
    /* Get choice: syscall(14) -> rax */
    p = emit_syscall_only(p, 14);
    /* Save to r12 */
    *p++ = 0x49; *p++ = 0x89; *p++ = 0xC4; /* mov r12, rax */
    
    /* Echo choice */
    *p++ = 0x4C; *p++ = 0x89; *p++ = 0xE7; /* mov rdi, r12 */
    p = emit_syscall_only(p, 15);
    
    /* Print newline */
    p = emit_print(p, s_nl, s_nl_len);
    
    /* Check if choice is '3' (exit) - skip username/password prompts */
    *p++ = 0x41; *p++ = 0x80; *p++ = 0xFC; *p++ = '3'; /* cmp r12b, '3' */
    *p++ = 0x0F; *p++ = 0x84; uint8_t *jexit_early = p; p += 4; /* je near exit (32-bit) */
    
    /* Print "Username: " */
    p = emit_print(p, s_user, s_user_len);
    
    /* Read username into ubuf */
    p = emit_mov64(p, 13, ubuf); /* r13 = buffer pointer */
    
    /* Loop: getchar, check enter, store, echo, repeat */
    uint8_t *uloop = p;
    p = emit_syscall_only(p, 14); /* getchar */
    *p++ = 0x3C; *p++ = 0x0D; /* cmp al, 13 (CR) */
    *p++ = 0x74; uint8_t *udone = p; *p++ = 0x00; /* je done */
    *p++ = 0x3C; *p++ = 0x0A; /* cmp al, 10 (LF) */
    *p++ = 0x74; uint8_t *udone2 = p; *p++ = 0x00; /* je done */
    /* store: mov [r13], al */
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    /* inc r13 */
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    /* echo: mov rdi, rax; syscall(15) */
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7;
    p = emit_syscall_only(p, 15);
    /* jmp uloop */
    *p++ = 0xEB;
    int8_t uloop_off = (int8_t)(uloop - p - 1);
    *p++ = (uint8_t)uloop_off;
    /* udone: */
    *udone = (uint8_t)(p - udone - 1);
    *udone2 = (uint8_t)(p - udone2 - 1);
    /* null terminate */
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    
    /* Print newline */
    p = emit_print(p, s_nl, s_nl_len);
    
    /* Print "Password: " */
    p = emit_print(p, s_pass, s_pass_len);
    
    /* Read password into pbuf */
    p = emit_mov64(p, 13, pbuf);
    
    uint8_t *ploop = p;
    p = emit_syscall_only(p, 14);
    *p++ = 0x3C; *p++ = 0x0D;
    *p++ = 0x74; uint8_t *pdone = p; *p++ = 0x00;
    *p++ = 0x3C; *p++ = 0x0A;
    *p++ = 0x74; uint8_t *pdone2 = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x88; *p++ = 0x45; *p++ = 0x00;
    *p++ = 0x49; *p++ = 0xFF; *p++ = 0xC5;
    /* echo '*' */
    *p++ = 0x48; *p++ = 0xC7; *p++ = 0xC7; *p++ = 0x2A; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    p = emit_syscall_only(p, 15);
    *p++ = 0xEB;
    int8_t ploop_off = (int8_t)(ploop - p - 1);
    *p++ = (uint8_t)ploop_off;
    *pdone = (uint8_t)(p - pdone - 1);
    *pdone2 = (uint8_t)(p - pdone2 - 1);
    *p++ = 0x41; *p++ = 0xC6; *p++ = 0x45; *p++ = 0x00; *p++ = 0x00;
    
    /* Print newline */
    p = emit_print(p, s_nl, s_nl_len);
    
    /* Check choice: r12 == '1' (login), '2' (register), else exit */
    *p++ = 0x41; *p++ = 0x80; *p++ = 0xFC; *p++ = '1'; /* cmp r12b, '1' */
    *p++ = 0x74; uint8_t *jlogin = p; *p++ = 0x00;
    *p++ = 0x41; *p++ = 0x80; *p++ = 0xFC; *p++ = '2'; /* cmp r12b, '2' */
    *p++ = 0x74; uint8_t *jreg = p; *p++ = 0x00;
    /* jmp near exit (use 32-bit relative jump since exit is far) */
    *p++ = 0xE9; uint8_t *jexit1 = p; p += 4; /* jmp rel32 */
    
    /* Login: */
    *jlogin = (uint8_t)(p - jlogin - 1);
    p = emit_mov64(p, 7, ubuf); /* rdi = username */
    p = emit_mov64(p, 6, pbuf); /* rsi = password */
    p = emit_syscall_only(p, 10); /* SYS_PASS_VERIFY */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x75; uint8_t *jfail = p; *p++ = 0x00; /* jne fail */
    /* success: */
    p = emit_print(p, s_ok, s_ok_len);
    /* jmp near exit */
    *p++ = 0xE9; uint8_t *jexit2 = p; p += 4;
    /* fail: */
    *jfail = (uint8_t)(p - jfail - 1);
    p = emit_print(p, s_fail, s_fail_len);
    /* jmp near exit */
    *p++ = 0xE9; uint8_t *jexit3 = p; p += 4;
    
    /* Register: */
    *jreg = (uint8_t)(p - jreg - 1);
    p = emit_mov64(p, 7, ubuf);
    p = emit_syscall_only(p, 12); /* SYS_PASS_EXISTS */
    *p++ = 0x48; *p++ = 0x85; *p++ = 0xC0; /* test rax, rax */
    *p++ = 0x75; uint8_t *jexists = p; *p++ = 0x00; /* jne exists */
    /* store new user */
    p = emit_mov64(p, 7, ubuf);
    p = emit_mov64(p, 6, pbuf);
    p = emit_syscall_only(p, 11); /* SYS_PASS_STORE */
    p = emit_print(p, s_reg, s_reg_len);
    /* jmp near exit */
    *p++ = 0xE9; uint8_t *jexit4 = p; p += 4;
    /* exists: */
    *jexists = (uint8_t)(p - jexists - 1);
    p = emit_print(p, s_exists, s_exists_len);
    
    /* Exit: (fall through from exists, or jump here) */
    uint8_t *exit_label = p;
    /* Patch all near jumps to exit */
    {
        int32_t off_early = (int32_t)(exit_label - (jexit_early + 4));
        jexit_early[0] = off_early & 0xFF;
        jexit_early[1] = (off_early >> 8) & 0xFF;
        jexit_early[2] = (off_early >> 16) & 0xFF;
        jexit_early[3] = (off_early >> 24) & 0xFF;
        
        int32_t off1 = (int32_t)(exit_label - (jexit1 + 4));
        jexit1[0] = off1 & 0xFF;
        jexit1[1] = (off1 >> 8) & 0xFF;
        jexit1[2] = (off1 >> 16) & 0xFF;
        jexit1[3] = (off1 >> 24) & 0xFF;
        
        int32_t off2 = (int32_t)(exit_label - (jexit2 + 4));
        jexit2[0] = off2 & 0xFF;
        jexit2[1] = (off2 >> 8) & 0xFF;
        jexit2[2] = (off2 >> 16) & 0xFF;
        jexit2[3] = (off2 >> 24) & 0xFF;
        
        int32_t off3 = (int32_t)(exit_label - (jexit3 + 4));
        jexit3[0] = off3 & 0xFF;
        jexit3[1] = (off3 >> 8) & 0xFF;
        jexit3[2] = (off3 >> 16) & 0xFF;
        jexit3[3] = (off3 >> 24) & 0xFF;
        
        int32_t off4 = (int32_t)(exit_label - (jexit4 + 4));
        jexit4[0] = off4 & 0xFF;
        jexit4[1] = (off4 >> 8) & 0xFF;
        jexit4[2] = (off4 >> 16) & 0xFF;
        jexit4[3] = (off4 >> 24) & 0xFF;
    }
    p = emit_print(p, s_bye, s_bye_len);
    p = emit_syscall_only(p, 1); /* SYSCALL_TEST - returns to kernel */
    *p++ = 0xEB; *p++ = 0xFE; /* infinite loop fallback */
    
    uart_puts("Generated bytes of code.\n");
    vga_write("Default user: admin / 1234\n\n");
    vga_write("Jumping to Ring 3...\n\n");
    
    /* Save kernel context */
    usermode_pass_completed = 0;
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
        vga_write("\n=== Returned from Ring 3 ===\n");
        vga_write("Press ESC to return to menu.\n");
        /* Wait for ESC */
        while (1) {
            char c = keyboard_getchar();
            if (c == 27) break;
            __asm__ volatile("pause");
        }
        return;
    }
    
    /* Mask timer AND keyboard before entering user mode - we'll use polling only */
    uint8_t pic_mask = hal_inb(0x21);
    hal_outb(0x21, pic_mask | 0x03);  /* IRQ0=timer, IRQ1=keyboard */
    
    /* Flush any pending keyboard data */
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60);  /* discard */
    }
    
    /* Jump to Ring 3 */
    uint64_t user_ss = 0x1B;
    uint64_t user_rsp = UPASS_STACK - 8;
    uint64_t user_rflags = 0x202;
    uint64_t user_cs = 0x23;
    uint64_t user_rip = UPASS_CODE;
    
    __asm__ volatile(
        "cli\n"
        "mov $0x1B, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "push %0\n"       /* SS */
        "push %1\n"       /* RSP */
        "push %2\n"       /* RFLAGS */
        "push %3\n"       /* CS */
        "push %4\n"       /* RIP */
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
    
    #undef UPASS_CODE
    #undef UPASS_DATA
    #undef UPASS_BUF
    #undef UPASS_STACK
}
