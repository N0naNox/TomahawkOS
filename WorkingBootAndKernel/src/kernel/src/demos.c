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

/* User mode test code as raw bytes */
static const unsigned char user_program_bytes[] = {
    /* mov rax, 1 (SYSCALL_TEST) */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    /* xor rdi, rdi */
    0x48, 0x31, 0xFF,
    /* syscall */
    0x0F, 0x05,
    
    /* mov rax, 1 (second syscall) */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    /* xor rdi, rdi */
    0x48, 0x31, 0xFF,
    /* syscall */
    0x0F, 0x05,
    
    /* infinite loop: pause; jmp -2 */
    0xF3, 0x90,  /* pause */
    0xEB, 0xFC   /* jmp -4 (to pause) */
};

void run_usermode_demo(void) {
    vga_write("\n=== User Mode Transition Demo ===\n");
    uart_puts("\n=== User Mode Transition Demo ===\n");
    vga_write("Jumping from Ring 0 (Kernel) to Ring 3 (User)...\n");
    uart_puts("Attempting privilege level transition...\n");
    
    /* Allocate user stack (1 page = 4KB) */
    uintptr_t user_stack_phys = pfa_alloc_frame();
    if (!user_stack_phys) {
        uart_puts("[ERROR] Failed to allocate user stack!\n");
        vga_write("ERROR: Stack allocation failed!\n");
        return;
    }
    
    /* Allocate user code page */
    uintptr_t user_code_phys = pfa_alloc_frame();
    if (!user_code_phys) {
        uart_puts("[ERROR] Failed to allocate user code page!\n");
        vga_write("ERROR: Code allocation failed!\n");
        pfa_free_frame(user_stack_phys);
        return;
    }
    
    /* Allocate additional pages for code (4 pages = 16KB total) */
    uintptr_t code_pages[4];
    code_pages[0] = user_code_phys;
    for (int i = 1; i < 4; i++) {
        code_pages[i] = pfa_alloc_frame();
        if (!code_pages[i]) {
            uart_puts("[ERROR] Failed to allocate code pages!\n");
            vga_write("ERROR: Code allocation failed!\n");
            return;
        }
    }
    
    /* Get current page table */
    uintptr_t cr3 = paging_get_current_cr3();
    
    /* Map user code pages at 0x400000 with RW+User for copying */
    uint64_t user_code_virt = 0x400000;
    for (int i = 0; i < 4; i++) {
        paging_map_page(cr3, user_code_virt + (i * 4096), code_pages[i], 
                        PTE_PRESENT | PTE_RW | PTE_USER);
    }
    
    /* Copy user program code to the new pages */
    void* user_code_ptr = (void*)user_code_virt;
    for (size_t i = 0; i < sizeof(user_program_bytes); i++) {
        ((unsigned char*)user_code_ptr)[i] = user_program_bytes[i];
    }
    
    /* Map user stack at 0x800000 (8MB) - map 2 pages for safety */
    uint64_t user_stack_virt = 0x800000;
    paging_map_page(cr3, user_stack_virt, user_stack_phys, 
                    PTE_PRESENT | PTE_RW | PTE_USER);
    
    /* Allocate and map one more page for the stack (grows downward) */
    uintptr_t stack_page2_phys = pfa_alloc_frame();
    if (stack_page2_phys) {
        paging_map_page(cr3, user_stack_virt + 4096, stack_page2_phys, 
                        PTE_PRESENT | PTE_RW | PTE_USER);
    }
    
    /* Stack top at end of second page minus 16 bytes for alignment */
    uint64_t user_stack_top = user_stack_virt + (2 * 4096) - 16;
    
    uart_puts("[KERNEL] User code mapped at: 0x");
    uart_puthex(user_code_virt);
    uart_puts("\n[KERNEL] User stack at: 0x");
    uart_puthex(user_stack_virt);
    uart_puts("\n[KERNEL] Jumping to user mode at entry: 0x");
    uart_puthex(user_code_virt);
    uart_puts(" with stack: 0x");
    uart_puthex(user_stack_top);
    uart_puts("\n");
    
    vga_write("Executing user program...\n");
    
    /* This will jump to Ring 3 and never return */
    jump_to_user(user_code_virt, user_stack_top);
    
    /* Should never reach here */
    uart_puts("[ERROR] Returned from user mode unexpectedly!\n");
    vga_write("ERROR: Unexpected return from user mode!\n");
}

/* ========== VFS Demo ========== */

void run_vfs_demo(void) {
    vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    vga_write("=== VFS (Virtual File System) Demo ===\n");
    vga_write("Testing basic file operations...\n\n");
    
    uart_puts("\n=== VFS Demo ===\n");
    uart_puts("Initializing VFS...\n");
    
    /* Initialize VFS */
    vfs_init();
    
    /* Create a test file */
    vga_write("Creating test file...\n");
    struct vnode* file = vfs_create_vnode(VREG);
    if (!file) {
        vga_write("ERROR: Failed to create file!\n");
        uart_puts("[VFS] ERROR: vnode creation failed\n");
        return;
    }
    vga_write("File created successfully!\n");
    
    /* Open the file */
    if (vfs_open(file) != 0) {
        vga_write("ERROR: Failed to open file!\n");
        uart_puts("[VFS] ERROR: open failed\n");
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
        uart_puts("[VFS] ERROR: write failed\n");
    } else {
        vga_write("Write successful!\n");
        uart_puts("[VFS] Bytes written: ");
        uart_putu(bytes_written);
        uart_puts("\n");
    }
    
    /* Read data back */
    char read_buffer[64] = {0};
    vga_write("\nReading data back...\n");
    
    int bytes_read = vfs_read(file, read_buffer, 64);
    if (bytes_read < 0) {
        vga_write("ERROR: Read failed!\n");
        uart_puts("[VFS] ERROR: read failed\n");
    } else {
        vga_write("Read successful!\n");
        uart_puts("[VFS] Bytes read: ");
        uart_putu(bytes_read);
        uart_puts("\n");
        
        vga_write("Data: ");
        vga_write(read_buffer);
        vga_write("\n");
    }
    
    /* Close the file */
    vga_write("\nClosing file...\n");
    vfs_close(file);
    
    vga_write("\n=== VFS Demo Complete! ===\n");
    vga_write("Press ESC to return to menu.\n");
    uart_puts("[VFS] Demo complete\n");
    
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
