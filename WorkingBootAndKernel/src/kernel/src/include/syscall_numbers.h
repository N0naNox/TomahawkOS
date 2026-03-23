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

/* WAITPID */
#define SYS_WAITPID 60

#define SYS_FAT32_TOUCH 61

/* Line editing with cursor support */
#define SYS_READLINE        70
#define SYS_READLINE_MASKED 71

/* Socket syscalls (BSD-style) */
#define SYS_SOCKET      80   /* socket(domain, type, protocol) -> fd   */
#define SYS_BIND        81   /* bind(fd, *sockaddr_in) -> 0 or err     */
#define SYS_SENDTO      82   /* sendto(fd, *socket_io_args_t) -> bytes */
#define SYS_RECVFROM    83   /* recvfrom(fd, *socket_io_args_t) -> bytes */
#define SYS_CONNECT     84   /* connect(fd, *sockaddr_in) -> 0 or err  */
#define SYS_SEND        85   /* send(fd, buf, len) -> bytes            */
#define SYS_RECV        86   /* recv(fd, buf, maxlen) -> bytes         */
#define SYS_SOCK_CLOSE  87   /* sock_close(fd) -> 0 or err            */

/* System power */
#define SYS_SHUTDOWN    90   /* shutdown the machine                   */

/* User/password management */
#define SYS_PASS_CHANGE 91   /* change own password (old, new)         */
#define SYS_PASS_DELETE 92   /* delete a user by name (root only)      */
#define SYS_PROMOTE_USER 93  /* grant admin to user (admin only)       */
#define SYS_DEMOTE_USER  94  /* revoke admin from user (admin only)    */

/* File descriptor syscalls */
#define SYS_OPEN   100  /* open(path, flags)  → fd or -errno      */
#define SYS_READ   101  /* read(fd, buf, len) → bytes or -errno   */
#define SYS_CLOSE  102  /* close(fd)          → 0 or -errno       */
#define SYS_WRITE_FD 103 /* write(fd, buf, len) → bytes or -errno */
#define SYS_DUP    104  /* dup(fd)            → new_fd or -errno  */
#define SYS_PIPE   105  /* pipe(fds[2])       → 0 or -errno       */
#define SYS_GETCWD 106  /* getcwd(buf, size)  → len or -errno     */
