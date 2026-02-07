#pragma once
#include <stdint.h>
#include "vnode.h"

struct file
{
    struct vnode* f_vnode;   /* Pointer to associated vnode */
    uint32_t f_offset;       /* Current read/write position */
    uint16_t f_flags;        /* Flags (e.g. O_RDONLY) */
    int refcount;            /* Reference count */
};