/*
 * demos.h - Phase 4 demo function declarations
 */

#ifndef DEMOS_H
#define DEMOS_H

/* Shared demo state */
extern volatile int demo_stop_requested;
extern void demo_esc_watcher(void);

/* Run Copy-On-Write fork demo */
void run_cow_fork_demo(void);

/* Run signal handling demo */
void run_signal_demo(void);

/* Run combined COW + signals demo */
void run_combined_cow_signals_demo(void);

/* Run user mode transition demo */
void run_usermode_demo(void);

/* Run VFS demo */
void run_vfs_demo(void);

/* Run Tomahawk Shell - interactive shell in Ring 3 */
void run_tomahawk_shell(void);

/* Run scheduler demo - shows multi-threaded scheduling */
void run_scheduler_demo(void);

/* Run FAT32 filesystem demo - reads from real ATA disk */
void run_fat32_demo(void);

#endif /* DEMOS_H */
