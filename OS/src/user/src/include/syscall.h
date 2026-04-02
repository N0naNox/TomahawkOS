#pragma once

#include <stdint.h>
#include <stddef.h>

/* ---------- low-level syscall helpers ---------- */
static inline uint64_t _syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx","r11","memory");
    return ret;
}
static inline uint64_t _syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx","r11","memory");
    return ret;
}
static inline uint64_t _syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return ret;
}
static inline uint64_t _syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return ret;
}

/* ---------- syscall numbers (must match kernel) ---------- */
#define SYS_YIELD    2
#define SYS_EXIT     3
#define SYS_FORK     4
#define SYS_GETPID   5
#define SYS_WRITE    13
#define SYS_OPEN    100
#define SYS_READ    101
#define SYS_CLOSE   102
#define SYS_WRITE_FD 103
#define SYS_DUP     104
#define SYS_PIPE    105

/* ---------- wrappers ---------- */
void     sys_yield(void);
void     sys_exit(int code);
int      sys_fork(void);
int      sys_getpid(void);
int      sys_open(const char *path, int flags);
int      sys_read(int fd, void *buf, size_t len);
int      sys_write(int fd, const void *buf, size_t len);
int      sys_close(int fd);
int      sys_dup(int fd);
int      sys_pipe(int fds[2]);
