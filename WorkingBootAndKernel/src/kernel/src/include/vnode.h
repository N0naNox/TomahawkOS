#pragma once
#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"

/* Maximum filename length */
#define VFS_NAME_MAX 64

enum vtype {
    VNON,   /* No type */
    VREG,   /* Regular file */
    VDIR,   /* Directory */
    VCHR,   /* Character device */
    VBLK    /* Block device */
};

/**
 * @brief Directory entry - links a name to a child vnode
 */
struct dir_entry {
    char name[VFS_NAME_MAX];
    struct vnode *vnode;
    struct dir_entry *next;
};

struct vnode {
    enum vtype v_type;      /* VREG (file), VDIR (directory), VCHR (device) */
    struct vnode_ops *v_op; /* vnode operations (function pointers) */
    void* v_data;           /* usually points to struct inode */
    struct vnode *v_parent; /* parent directory (NULL for root) */

    /* Directory children (only used when v_type == VDIR) */
    struct dir_entry *v_children;
    int v_nchildren;

    /**
     * Per-vnode spinlock.
     * Protects: v_children, v_nchildren, v_parent, and vnode lifecycle
     * (allocation / free).  Always acquire with spin_lock_irqsave so that
     * interrupt handlers that traverse the VFS tree cannot deadlock.
     */
    spinlock_t v_lock;
};

struct vnode_ops {
    int (*vop_open)(struct vnode *vp);
    int (*vop_read)(struct vnode *vp, void *buf, size_t nbyte);
    int (*vop_write)(struct vnode *vp, void *buf, size_t nbyte);
    // More functions may be added as needed
};