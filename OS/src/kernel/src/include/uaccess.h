#ifndef UACCESS_H
#define UACCESS_H

#include <stdint.h>
#include <stddef.h>

/*
 * User-space address boundary.
 * On x86-64, canonical user addresses are [0, 0x00007FFFFFFFFFFF].
 * Anything at or above 0x0000800000000000 is either the non-canonical
 * hole or kernel space and must be rejected.
 */
#define USER_ADDR_MAX    0x0000800000000000ULL

/**
 * Validate that [ptr, ptr+len) lies entirely in user-space and does not
 * wrap around.  Returns 0 on success, -EFAULT on bad pointer.
 */
static inline int validate_user_buf(const void *ptr, size_t len)
{
    uintptr_t start = (uintptr_t)ptr;
    if (!ptr)                       return -14; /* EFAULT */
    if (start >= USER_ADDR_MAX)     return -14;
    if (start + len < start)        return -14; /* wrap */
    if (start + len > USER_ADDR_MAX) return -14;
    return 0;
}

/**
 * Validate a NUL-terminated user string.
 * Checks that the string starts in user-space.  Does NOT walk the
 * entire string (that would risk a page fault if it's very long).
 */
static inline int validate_user_string(const char *s)
{
    if (!s) return -14;
    if ((uintptr_t)s >= USER_ADDR_MAX) return -14;
    return 0;
}

#endif /* UACCESS_H */
