#pragma once

#define SYSCALL_TEST 1
#define SYS_YIELD 2
#define SYS_EXIT 3
#define SYS_FORK 4
#define SYS_GETPID 5
#define SYS_SIGNAL 6
#define SYS_KILL 7
#define SYS_SIGPROCMASK 8
#define SYS_SIGRETURN 9
#define SYS_PASS_VERIFY 10
#define SYS_PASS_STORE 11
#define SYS_PASS_EXISTS 12
#define SYS_WRITE 13
#define SYS_GETCHAR 14
#define SYS_PUTCHAR 15
#define SYS_GETUID 16
#define SYS_SETUID 17
#define SYS_GET_USERNAME 18
#define SYS_CLEAR_SCREEN 19
#define SYS_SHELL_EXIT 20
#define SYS_PASS_GET_UID 21

/* Demo syscalls - run kernel demos from usermode shell */
/* Note: scheduler, COW, and signal demos removed - they require preemptive
   scheduling which corrupts the syscall return path when context switches occur */
#define SYS_RUN_VFS_DEMO 25
#define SYS_RUN_TESTS 26
#define SYS_RUN_FAT32_DEMO 27

/* FAT32 shell commands */
#define SYS_FAT32_MOUNT  30
#define SYS_FAT32_UMOUNT 31
#define SYS_FAT32_LS     32
#define SYS_FAT32_CAT    33
#define SYS_FAT32_WRITE  34
#define SYS_FAT32_MKDIR  35
#define SYS_FAT32_RM     36
#define SYS_FAT32_CD     37