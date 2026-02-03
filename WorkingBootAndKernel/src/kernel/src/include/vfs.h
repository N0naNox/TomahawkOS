#pragma once
#include <stdint.h>
#include "vnode.h"
#include "inode.h"

/* Forward declaration */
struct block_device;

/* VFS initialization */
void vfs_init(void);

/* Set the default block device for VFS storage */
void vfs_set_backing_device(struct block_device *dev);

/* Get the default block device */
struct block_device *vfs_get_backing_device(void);

/* Create a new vnode (uses default backing device) */
struct vnode* vfs_create_vnode(enum vtype type);

/* Create a new vnode with specific block device */
struct vnode* vfs_create_vnode_on_device(enum vtype type, struct block_device *dev);

/* Free a vnode */
void vfs_free_vnode(struct vnode* vp);

/* Get root vnode */
struct vnode* vfs_get_root(void);

/* VFS operations */
int vfs_open(struct vnode* vp);
int vfs_read(struct vnode* vp, void* buf, size_t nbyte);
int vfs_write(struct vnode* vp, const void* buf, size_t nbyte);
int vfs_close(struct vnode* vp);
