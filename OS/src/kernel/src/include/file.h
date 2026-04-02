#pragma once
#include <stdint.h>
#include "vnode.h"

/* Open flags */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_ACCMODE  0x0003  /* mask for read/write bits */

struct file
{
    struct vnode* f_vnode;   /* Pointer to associated vnode */
    uint32_t f_offset;       /* Current read/write position */
    uint16_t f_flags;        /* Flags (e.g. O_RDONLY) */
    int refcount;            /* Reference count */
};