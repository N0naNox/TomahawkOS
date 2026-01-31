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

/* Static allocation for user pages - allocated once */
static uintptr_t user_code_phys_static = 0;
static uintptr_t user_stack_phys1_static = 0;
static uintptr_t user_stack_phys2_static = 0;
static int usermode_pages_initialized = 0;

void run_usermode_demo(void) {
    vga_write("\n=== User Mode Transition Demo ===\n");
    vga_write("Testing Ring 0 -> Ring 3 privilege transition...\n\n");
    
    uintptr_t user_code_phys, user_stack_phys1, user_stack_phys2;
    
    /* Only allocate pages on first run */
    if (!usermode_pages_initialized) {
        /* Allocate user code page */
        user_code_phys = pfa_alloc_frame();
        if (!user_code_phys) {
            vga_write("ERROR: Code allocation failed!\n");
            return;
        }
        
        /* Allocate TWO pages for user stack (8KB total) */
        user_stack_phys1 = pfa_alloc_frame();
        if (!user_stack_phys1) {
            vga_write("ERROR: Stack allocation failed!\n");
            pfa_free_frame(user_code_phys);
            return;
        }
        
        user_stack_phys2 = pfa_alloc_frame();
        if (!user_stack_phys2) {
            vga_write("ERROR: Stack allocation failed!\n");
            pfa_free_frame(user_code_phys);
            pfa_free_frame(user_stack_phys1);
            return;
        }
        
        /* Save for reuse */
        user_code_phys_static = user_code_phys;
        user_stack_phys1_static = user_stack_phys1;
        user_stack_phys2_static = user_stack_phys2;
        
        /* Get current page table */
        uintptr_t cr3 = paging_get_current_cr3();
        
        /* Map user code at 0x40000000 (1GB) */
        uint64_t user_code_virt = 0x40000000;
        paging_map_page(cr3, user_code_virt, user_code_phys, 
                        PTE_PRESENT | PTE_RW | PTE_USER);
        
        /* Map user stack at 0x41000000 with USER permissions - TWO pages */
        uint64_t user_stack_virt = 0x41000000;
        paging_map_page(cr3, user_stack_virt, user_stack_phys1, 
                        PTE_PRESENT | PTE_RW | PTE_USER);
        paging_map_page(cr3, user_stack_virt + 4096, user_stack_phys2, 
                        PTE_PRESENT | PTE_RW | PTE_USER);
        
        usermode_pages_initialized = 1;
    } else {
        user_code_phys = user_code_phys_static;
    }
    
    /* Always copy the user program (in case memory was corrupted) */
    void* user_code_ptr_phys = (void*)user_code_phys;
    for (size_t i = 0; i < sizeof(user_program_minimal); i++) {
        ((unsigned char*)user_code_ptr_phys)[i] = user_program_minimal[i];
    }
    
    uint64_t user_code_virt = 0x40000000;
    uint64_t user_stack_virt = 0x41000000;
    
    /* Flush TLB for the user pages */
    __asm__ volatile("invlpg (%0)" :: "r"(user_code_virt) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(user_stack_virt) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(user_stack_virt + 4096) : "memory");
    
    /* Stack top at end of SECOND page minus 16 bytes for alignment */
    uint64_t user_stack_top = user_stack_virt + (2 * 4096) - 16;
    
    vga_write("User code mapped at: 0x40000000\n");
    vga_write("User stack at: 0x41001FF0\n");
    vga_write("\nJumping to Ring 3 (user mode)...\n");
    vga_write("User program will make syscall back to kernel.\n\n");
    
    /* Reset completion flag BEFORE saving context */
    usermode_demo_completed = 0;
    
    /* Save return context - syscall handler will jump back here */
    __asm__ volatile(
        "mov %%rsp, %0\n"
        "mov %%rbp, %1\n"
        "lea 1f(%%rip), %%rax\n"
        "mov %%rax, %2\n"
        "jmp 2f\n"
        "1:\n"  /* Return point - syscall handler jumps here */
        : "=m"(usermode_demo_return_rsp), "=m"(usermode_demo_return_rbp), "=m"(usermode_demo_return_rip)
        :: "rax", "memory"
    );
    
    /* Check if we're returning from syscall */
    if (usermode_demo_completed) {
        /* Syscall handler jumped here after displaying messages and waiting for ESC */
        return;
    }
    
    __asm__ volatile("2:\n" ::: "memory");  /* Continue point for initial execution */
    
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
    vga_write("=== VFS (Virtual File System) Demo ===\n");
    vga_write("Testing basic file operations...\n\n");
    
    /* Initialize VFS */
    vfs_init();
    
    /* Create a test file */
    vga_write("Creating test file...\n");
    struct vnode* file = vfs_create_vnode(VREG);
    if (!file) {
        vga_write("ERROR: Failed to create file!\n");
        return;
    }
    vga_write("File created successfully!\n");
    
    /* Open the file */
    if (vfs_open(file) != 0) {
        vga_write("ERROR: Failed to open file!\n");
        vfs_free_vnode(file);
        return;
    }
    vga_write("File opened successfully!\n");
    
    /* Write data to the file */
    const char* test_data = "Hello from VFS! This is test data.";
    vga_write("Writing data: ");
    vga_write(test_data);
    vga_write("\n");
    
    int bytes_written = vfs_write(file, test_data, 35);
    if (bytes_written < 0) {
        vga_write("ERROR: Write failed!\n");
    } else {
        vga_write("Write successful!\n");
    }
    
    /* Read data back */
    char read_buffer[64] = {0};
    vga_write("\nReading data back...\n");
    
    int bytes_read = vfs_read(file, read_buffer, 64);
    if (bytes_read < 0) {
        vga_write("ERROR: Read failed!\n");
    } else {
        vga_write("Read successful!\n");
        vga_write("Data: ");
        vga_write(read_buffer);
        vga_write("\n");
    }
    
    /* Close the file */
    vga_write("\nClosing file...\n");
    vfs_close(file);
    
    vga_write("\n=== VFS Demo Complete! ===\n");
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
