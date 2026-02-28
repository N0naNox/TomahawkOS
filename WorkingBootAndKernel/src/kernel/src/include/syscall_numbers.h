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

/* Process execution syscalls */
#define SYS_EXEC 28
#define SYS_WAIT 29
#define SYS_RUN_FORK_EXEC_WAIT_DEMO 30

/* Shell filesystem command dispatch */
#define SYS_SHELL_CMD 31

/* Job control syscalls */
#define SYS_SETPGID    32
#define SYS_GETPGID    33
#define SYS_SETSID     34
#define SYS_TCSETPGRP  35   /* set foreground group for session */
#define SYS_TCGETPGRP  36   /* get foreground group for session */
#define SYS_RUN_JOB_CONTROL_DEMO 37

/* Filesystem metadata mutation syscalls */
#define SYS_UNLINK  38  /* arg1=parent_path, arg2=name */
#define SYS_RENAME  39  /* arg1=old_path,    arg2=new_path */
#define SYS_CHMOD   40  /* arg1=path,        arg2=mode */

/* FAT32 shell commands */
#define SYS_FAT32_MOUNT  50
#define SYS_FAT32_UMOUNT 51
#define SYS_FAT32_LS     52
#define SYS_FAT32_CAT    53
#define SYS_FAT32_WRITE  54
#define SYS_FAT32_MKDIR  55
#define SYS_FAT32_RM     56
#define SYS_FAT32_CD     57
#define SYS_FAT32_RENAME 58
#define SYS_FAT32_CHMOD  59

#define SYS_FAT32_TOUCH 61

/* WAITPID */
#define SYS_WAITPID 60
