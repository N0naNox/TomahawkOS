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

/* Run fork-exec-wait demo */
void run_fork_exec_wait_demo(void);

/* Run user mode transition demo */
void run_usermode_demo(void);

/* Run VFS demo */
void run_vfs_demo(void);

/* Run Tomahawk Shell - interactive shell in Ring 3 */
void run_tomahawk_shell(void);

/* Run scheduler demo - shows multi-threaded scheduling */
void run_scheduler_demo(void);

/* Run job control demo - foreground/background groups, Ctrl+C/Z */
void run_job_control_demo(void);

/* Run FAT32 filesystem demo - reads from real ATA disk */
void run_fat32_demo(void);

/* FAT32 shell commands - individual operations */
void shell_fat32_mount(const char *cmdline);
void shell_fat32_umount(const char *cmdline);
void shell_fat32_ls(const char *cmdline);
void shell_fat32_stat(const char *cmdline);
void shell_fat32_cat(const char *cmdline);
void shell_fat32_write(const char *cmdline);
void shell_fat32_mkdir(const char *cmdline);
void shell_fat32_rm(const char *cmdline);
void shell_fat32_cd(const char *cmdline);
void shell_fat32_rename(const char *cmdline);
void shell_fat32_chmod(const char *cmdline);
void shell_fat32_touch(const char *cmdline);
void shell_fat32_editinit(const char *cmdline);

/* Home directory and CWD path helpers */
const char *shell_fat32_get_cwd_path(void);
void shell_fat32_ensure_home(const char *username);
void shell_fat32_cd_to_home(const char *username);
void shell_fat32_delete_home(const char *username);

#endif /* DEMOS_H */
