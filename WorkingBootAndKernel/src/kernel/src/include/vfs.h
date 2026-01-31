#pragma once
#include <stdint.h>
#include "vnode.h"
#include "inode.h"

/* VFS initialization */
void vfs_init(void);

/* Create a new vnode */
struct vnode* vfs_create_vnode(enum vtype type);

/* Free a vnode */
void vfs_free_vnode(struct vnode* vp);

/* Get root vnode */
struct vnode* vfs_get_root(void);

/* VFS operations */
int vfs_open(struct vnode* vp);
int vfs_read(struct vnode* vp, void* buf, size_t nbyte);
int vfs_write(struct vnode* vp, const void* buf, size_t nbyte);
int vfs_close(struct vnode* vp);
