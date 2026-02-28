/**
 * @file socket.c  (userland)
 * @brief Thin syscall wrappers implementing the userland socket API.
 *
 * Each function issues a `syscall` instruction with the appropriate
 * syscall number and arguments, matching the kernel dispatch in syscall.c.
 *
 * Calling convention used (matches the kernel's syscall_entry.asm):
 *   rax = syscall number
 *   rdi = arg1
 *   rsi = arg2
 *   rdx = arg3
 *
 * Return value is in rax (sign-extended to 64 bits; negative values
 * represent SOCK_ERR_* error codes cast to int).
 */

#include "include/socket.h"

/* ====================================================================
 *  Internal helper macro — single-argument syscall
 * ==================================================================== */

/** Issue a syscall with 0 user arguments (besides the number). */
static inline int64_t __syscall0(uint64_t num)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/** Issue a syscall with 1 user argument. */
static inline int64_t __syscall1(uint64_t num, uint64_t a1)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/** Issue a syscall with 2 user arguments. */
static inline int64_t __syscall2(uint64_t num, uint64_t a1, uint64_t a2)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/** Issue a syscall with 3 user arguments. */
static inline int64_t __syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* ====================================================================
 *  socket — create a new socket descriptor
 * ==================================================================== */

int socket(int domain, int type, int protocol)
{
    return (int)__syscall3(SYS_SOCKET,
                           (uint64_t)domain,
                           (uint64_t)type,
                           (uint64_t)protocol);
}

/* ====================================================================
 *  bind — attach a local address/port to a socket
 * ==================================================================== */

int bind(int fd, const sockaddr_in_t *addr)
{
    return (int)__syscall2(SYS_BIND,
                           (uint64_t)fd,
                           (uint64_t)(uintptr_t)addr);
}

/* ====================================================================
 *  sendto — transmit a datagram to a specific destination
 * ==================================================================== */

int sendto(int fd, const void *buf, uint16_t len, const sockaddr_in_t *dest)
{
    /* Pack the extra args into a stack-allocated helper struct. */
    socket_io_args_t io;
    io.buf  = (void *)buf;
    io.len  = len;
    if (dest) io.addr = *dest;

    return (int)__syscall2(SYS_SENDTO,
                           (uint64_t)fd,
                           (uint64_t)(uintptr_t)&io);
}

/* ====================================================================
 *  recvfrom — receive a datagram and optionally get the sender address
 * ==================================================================== */

int recvfrom(int fd, void *buf, uint16_t maxlen, sockaddr_in_t *from)
{
    socket_io_args_t io;
    io.buf = buf;
    io.len = maxlen;

    int ret = (int)__syscall2(SYS_RECVFROM,
                              (uint64_t)fd,
                              (uint64_t)(uintptr_t)&io);

    /* Copy sender address back to caller if requested. */
    if (ret >= 0 && from)
        *from = io.addr;

    return ret;
}

/* ====================================================================
 *  connect — set a default remote address (UDP convenience / TCP future)
 * ==================================================================== */

int connect(int fd, const sockaddr_in_t *addr)
{
    return (int)__syscall2(SYS_CONNECT,
                           (uint64_t)fd,
                           (uint64_t)(uintptr_t)addr);
}

/* ====================================================================
 *  send — transmit on a connected socket
 * ==================================================================== */

int send(int fd, const void *buf, uint16_t len)
{
    return (int)__syscall3(SYS_SEND,
                           (uint64_t)fd,
                           (uint64_t)(uintptr_t)buf,
                           (uint64_t)len);
}

/* ====================================================================
 *  recv — receive from a connected socket
 * ==================================================================== */

int recv(int fd, void *buf, uint16_t maxlen)
{
    return (int)__syscall3(SYS_RECV,
                           (uint64_t)fd,
                           (uint64_t)(uintptr_t)buf,
                           (uint64_t)maxlen);
}

/* ====================================================================
 *  sock_close — release a socket and its resources
 * ==================================================================== */

int sock_close(int fd)
{
    return (int)__syscall1(SYS_SOCK_CLOSE, (uint64_t)fd);
}
