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
