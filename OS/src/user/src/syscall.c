#include "syscall.h"

void sys_yield(void)  { _syscall0(SYS_YIELD); }
void sys_exit(int code) { _syscall1(SYS_EXIT, (uint64_t)code); }
int  sys_fork(void)   { return (int)_syscall0(SYS_FORK); }
int  sys_getpid(void) { return (int)_syscall0(SYS_GETPID); }

int sys_open(const char *path, int flags) {
    return (int)_syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int sys_read(int fd, void *buf, size_t len) {
    return (int)_syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

int sys_write(int fd, const void *buf, size_t len) {
    return (int)_syscall3(SYS_WRITE_FD, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

int sys_close(int fd) { return (int)_syscall1(SYS_CLOSE, (uint64_t)fd); }
int sys_dup(int fd)   { return (int)_syscall1(SYS_DUP, (uint64_t)fd); }

int sys_pipe(int fds[2]) {
    return (int)_syscall1(SYS_PIPE, (uint64_t)fds);
}
