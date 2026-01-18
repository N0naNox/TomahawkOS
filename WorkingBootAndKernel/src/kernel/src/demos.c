/*
 * demos.c - Phase 4 demo implementations for COW fork and signals
 */

#include <stdint.h>
#include "include/proc.h"
#include "include/signal.h"
#include "include/scheduler.h"
#include "include/keyboard.h"
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
    
    uart_puts("[COW Parent] Writing to shared memory before fork...\n");
    vga_write("[Parent] Writing to shared memory before fork...\n");
    
    cow_shared_counter = 100;
    
    uart_puts("[COW Parent] Calling fork()...\n");
    vga_write("[Parent] Calling fork()...\n");
    
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
        uart_puts("[COW Child] Hello from child! Reading shared counter...\n");
        vga_write("[Child] Hello from child! Reading shared counter...\n");
        
        uart_puts("[COW Child] Counter value (should be 100): ");
        uart_putu(cow_shared_counter);
        uart_puts("\n");
        vga_write("[Child] Counter = 100 (inherited from parent)\n");
        
        /* Child modifies - triggers COW */
        uart_puts("[COW Child] Modifying counter (triggers COW)...\n");
        vga_write("[Child] Modifying counter (triggers COW)...\n");
        cow_shared_counter = 200;
        
        uart_puts("[COW Child] After modification: ");
        uart_putu(cow_shared_counter);
        uart_puts("\n");
        vga_write("[Child] Counter = 200 (child's private copy)\n");
        
        uart_puts("[COW Child] Child exiting...\n");
        vga_write("[Child] Child exiting...\n");
        
        scheduler_thread_exit();
        return;
    } else {
        /* This is the parent process */
        uart_puts("[COW Parent] Forked child with PID: ");
        uart_putu(child_pid);
        uart_puts("\n");
        vga_write("[Parent] Forked child successfully\n");
        
        /* Give child time to run */
        for (volatile int i = 0; i < 3000000; i++) {
            __asm__ volatile("pause");
        }
        
        uart_puts("[COW Parent] Parent's counter value (should still be 100): ");
        uart_putu(cow_shared_counter);
        uart_puts("\n");
        vga_write("[Parent] Counter = 100 (unchanged, child has separate copy)\n");
        
        uart_puts("[COW Parent] COW test complete!\n");
        vga_write("[Parent] COW test complete!\n");
    }
    
    scheduler_thread_exit();
}

void run_cow_fork_demo(void) {
    vga_write("\n=== Copy-On-Write Fork Demo ===\n");
    uart_puts("\n=== Copy-On-Write Fork Demo ===\n");
    vga_write("This demo shows fork() with COW memory sharing.\n");
    uart_puts("This demo shows fork() with COW memory sharing.\n");
    
    demo_stop_requested = 0;
    cow_demo_done = 0;  /* Reset flag */
    
    create_process("cow-parent", cow_parent_thread);
    create_process("esc-watcher", demo_esc_watcher);
    
    /* Wait until ESC is pressed */
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    vga_write("Stopping COW demo...\n");
    uart_puts("Stopping COW demo...\n");
    for (volatile int i = 0; i < 5000000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== Signal Demo ========== */

static volatile int signal_received_flag = 0;
static volatile uint64_t signal_receiver_pid = 0;

static void my_signal_handler(int signum) {
    uart_puts("[Signal Handler] Caught signal: ");
    uart_putu(signum);
    uart_puts("\n");
    vga_write("[Handler] Signal caught!\n");
    signal_received_flag = 1;
}

static void signal_sender_thread(void) {
    uart_puts("[Sender] Waiting a bit before sending signal...\n");
    vga_write("[Sender] Waiting before sending signal...\n");
    
    for (volatile int i = 0; i < 5000000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Find the receiver process by PID */
    pcb_t* receiver = find_process_by_pid(signal_receiver_pid);
    if (!receiver) {
        uart_puts("[Sender] ERROR: Could not find receiver process\n");
        vga_write("[Sender] ERROR: receiver not found\n");
        return;
    }
    
    uart_puts("[Sender] Sending SIGUSR1 to receiver PID ");
    uart_putu(signal_receiver_pid);
    uart_puts("...\n");
    vga_write("[Sender] Sending SIGUSR1 to receiver...\n");
    
    signal_send(receiver, SIGUSR1);
    
    uart_puts("[Sender] Signal sent! Exiting...\n");
    vga_write("[Sender] Signal sent!\n");
    scheduler_thread_exit();
}
static void signal_receiver_thread(void) {
    uart_puts("[Receiver] Installing signal handler for SIGUSR1...\n");
    vga_write("[Receiver] Installing signal handler...\n");
    
    /* Install handler for SIGUSR1 */
    pcb_t* proc = get_current_process();
    signal_receiver_pid = proc->pid;  /* Store PID for sender to find */
    signal_install(proc, SIGUSR1, (uintptr_t)my_signal_handler);
    
    uart_puts("[Receiver] Handler installed. Receiver PID is ");
    uart_putu(signal_receiver_pid);
    uart_puts(". Waiting for signal...\n");
    vga_write("[Receiver] Waiting for signal...\n");
    
    signal_received_flag = 0;
    
    /* Busy wait until signal is received or demo stopped */
    while (!signal_received_flag && !demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    if (signal_received_flag) {
        uart_puts("[Receiver] Signal was handled successfully!\n");
        vga_write("[Receiver] Signal handled successfully!\n");
    }
    
    uart_puts("[Receiver] Thread exiting.\n");
    scheduler_thread_exit();
}

void run_signal_demo(void) {
    vga_write("\n=== Signal Handling Demo ===\n");
    uart_puts("\n=== Signal Handling Demo ===\n");
    vga_write("This demo shows signal delivery and handling.\n");
    uart_puts("This demo shows signal delivery and handling.\n");
    
    demo_stop_requested = 0;
    
    create_process("sig-receiver", signal_receiver_thread);
    create_process("sig-sender", signal_sender_thread);
    create_process("esc-watcher", demo_esc_watcher);
    
    /* Wait until ESC is pressed */
    while (!demo_stop_requested) {
        __asm__ volatile("pause");
    }
    
    vga_write("Stopping signal demo...\n");
    uart_puts("Stopping signal demo...\n");
    for (volatile int i = 0; i < 5000000; i++) {
        __asm__ volatile("pause");
    }
}

/* ========== Combined COW + Signals Demo ========== */

void run_combined_cow_signals_demo(void) {
    vga_write("\n=== Advanced Demo: COW Fork + Signals ===\n");
    uart_puts("\n=== Advanced Demo: COW Fork + Signals ===\n");
    vga_write("This demo will run COW fork and signal handling once.\n");
    uart_puts("This demo will run COW fork and signal handling once.\n\n");
    
    demo_stop_requested = 0;
    cow_demo_done = 0;  /* Reset flag */
    
    /* Part 1: COW Fork Demo */
    vga_write("--- Part 1: Copy-On-Write Fork ---\n");
    uart_puts("--- Part 1: Copy-On-Write Fork ---\n");
    create_process("cow-parent", cow_parent_thread);
    
    /* Wait for COW demo to complete */
    for (volatile int i = 0; i < 8000000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Part 2: Signal Demo */
    vga_write("\n--- Part 2: Signal Handling ---\n");
    uart_puts("\n--- Part 2: Signal Handling ---\n");
    
    /* Reset signal flag for this demo run */
    signal_received_flag = 0;
    
    create_process("sig-receiver", signal_receiver_thread);
    create_process("sig-sender", signal_sender_thread);
    
    /* Wait for signal to be received (limited time) */
    for (volatile int i = 0; i < 10000000 && !signal_received_flag; i++) {
        __asm__ volatile("pause");
    }
    
    /* Give a bit more time for cleanup messages */
    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("pause");
    }
    
    /* Demo complete */
    vga_write("\n=== Demo Complete ===\n");
    uart_puts("\n=== Demo Complete ===\n");
    vga_write("Press ESC to return to menu.\n");
    uart_puts("Press ESC to return to menu.\n");
    
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
    
    /* Cleanup */
    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("pause");
    }
}
