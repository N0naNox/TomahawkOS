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

/* ========== Directory & Path Operations ========== */

/**
 * @brief Look up a child vnode by name within a directory
 * @param dir Parent directory vnode
 * @param name Child name to find
 * @return Child vnode or NULL if not found
 */
struct vnode* vfs_lookup(struct vnode *dir, const char *name);

/**
 * @brief Resolve an absolute path to a vnode (e.g. "/etc/passwd")
 * @param path Absolute path starting with '/'
 * @return vnode at that path or NULL
 */
struct vnode* vfs_resolve_path(const char *path);

/**
 * @brief Create a subdirectory inside a directory
 * @param parent Parent directory vnode
 * @param name Name for the new directory
 * @return New directory vnode or NULL on error
 */
struct vnode* vfs_mkdir(struct vnode *parent, const char *name);

/**
 * @brief Create a regular file inside a directory
 * @param parent Parent directory vnode
 * @param name Name for the new file
 * @return New file vnode or NULL on error
 */
struct vnode* vfs_create_file(struct vnode *parent, const char *name);

/**
 * @brief Add a child vnode to a directory under a given name
 * @param dir Parent directory
 * @param name Name for the entry
 * @param child Vnode to link
 * @return 0 on success, -1 on error
 */
int vfs_add_dirent(struct vnode *dir, const char *name, struct vnode *child);

/* ========== Metadata Mutation Operations ========== */

/**
 * @brief Remove a file or empty directory from its parent directory.
 *
 * Acquires parent->v_lock to remove the directory entry, then acquires
 * the target inode's i_lock to decrement the reference count.
 * If i_refcount reaches zero the vnode is freed.
 *
 * @param parent  Parent directory vnode.
 * @param name    Name of the entry to remove.
 * @return 0 on success, -1 on error (not found, or directory not empty).
 */
int vfs_unlink(struct vnode *parent, const char *name);

/**
 * @brief Rename (move) an entry within or across directories.
 *
 * Lock ordering to prevent deadlock: always lock the directory with the
 * numerically lower pointer address first.  If old_parent == new_parent
 * only one lock is taken.
 *
 * @param old_parent  Directory containing the source name.
 * @param old_name    Current name of the entry.
 * @param new_parent  Destination directory vnode.
 * @param new_name    New name for the entry.
 * @return 0 on success, -1 on error.
 */
int vfs_rename(struct vnode *old_parent, const char *old_name,
               struct vnode *new_parent, const char *new_name);

/**
 * @brief Change the permission mode bits of a vnode's inode.
 *
 * Acquires vp's inode i_lock for the read-modify-write of i_mode.
 *
 * @param vp    Target vnode.
 * @param mode  New mode bits (lower 12 bits, POSIX style).
 * @return 0 on success, -1 if vp is NULL.
 */
int vfs_chmod(struct vnode *vp, uint16_t mode);
