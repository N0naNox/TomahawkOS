#ifndef ERRNO_H
#define ERRNO_H

#define EPERM        1   /* Operation not permitted */
#define ENOENT       2   /* No such file or directory */
#define ESRCH        3   /* No such process */
#define EIO          5   /* I/O error */
#define EBADF        9   /* Bad file descriptor */
#define ENOMEM      12   /* Out of memory */
#define EACCES      13   /* Permission denied */
#define EFAULT      14   /* Bad address (invalid user pointer) */
#define EEXIST      17   /* File exists */
#define ENOTDIR     20   /* Not a directory */
#define EISDIR      21   /* Is a directory */
#define EINVAL      22   /* Invalid argument */
#define EMFILE      24   /* Too many open files */
#define ENOSPC      28   /* No space left on device */
#define ENOSYS      38   /* Function not implemented */
#define ENOTEMPTY   39   /* Directory not empty */
#define EADDRINUSE  98   /* Address already in use */
#define EMSGSIZE   90    /* Message too long */
#define ENOTSUP    95    /* Operation not supported */
#define EAGAIN     11    /* Try again */

#endif
