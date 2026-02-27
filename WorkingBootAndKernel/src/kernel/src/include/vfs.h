#pragma once
#include <stdint.h>
#include "vnode.h"
#include "inode.h"

/* Forward declaration */
struct block_device;

/* ========== VFS Initialization ========== */

void vfs_init(void);

/* Set/get the default block device for VFS storage */
void vfs_set_backing_device(struct block_device *dev);
struct block_device *vfs_get_backing_device(void);

/* ========== Vnode Lifecycle ========== */

/* Create a new vnode (uses default backing device) */
struct vnode *vfs_create_vnode(enum vtype type);

/* Create a new vnode with specific block device */
struct vnode *vfs_create_vnode_on_device(enum vtype type, struct block_device *dev);

/* Free a vnode */
void vfs_free_vnode(struct vnode *vp);

/* Get root vnode */
struct vnode *vfs_get_root(void);

/* ========== File Operations ========== */

int vfs_open(struct vnode *vp);
int vfs_close(struct vnode *vp);
int vfs_read(struct vnode *vp, void *buf, size_t nbyte);
int vfs_write(struct vnode *vp, const void *buf, size_t nbyte);

/* Offset-aware read/write */
int vfs_read_at(struct vnode *vp, void *buf, size_t nbyte, uint64_t offset);
int vfs_write_at(struct vnode *vp, const void *buf, size_t nbyte, uint64_t offset);

/* Get file size */
int64_t vfs_getsize(struct vnode *vp);

/* ========== Directory Operations ========== */

/**
 * Look up a child entry inside a directory vnode.
 * @return 0 on success (*result filled), negative on error
 */
int vfs_lookup(struct vnode *dir, const char *name, struct vnode **result);

/**
 * Read one directory entry at *offset, then advance *offset.
 * @return 0 = got entry, 1 = end of directory, negative = error
 */
int vfs_readdir(struct vnode *dir, struct vfs_dirent *dent, uint64_t *offset);

/**
 * Create a new file in a directory.
 * @return 0 on success (*result filled), negative on error
 */
int vfs_create(struct vnode *dir, const char *name, struct vnode **result);

/**
 * Create a new subdirectory.
 * @return 0 on success (*result filled), negative on error
 */
int vfs_mkdir(struct vnode *dir, const char *name, struct vnode **result);

/**
 * Remove a file or empty directory.
 * @return 0 on success, negative on error
 */
int vfs_remove(struct vnode *dir, const char *name);

/* ========== Path Resolution ========== */

/**
 * Resolve a full path (e.g. "/mnt/fat/subdir/file.txt") to a vnode.
 * Walks mount points and directory components using vop_lookup.
 * @param path  Absolute path (must start with '/')
 * @param result  On success, set to the resolved vnode
 * @return 0 on success, negative on error
 */
int vfs_resolve_path(const char *path, struct vnode **result);

/**
 * Resolve a full path, returning both the parent directory vnode
 * and the final filename component. Useful for create/mkdir/remove.
 * @param path      Absolute path
 * @param parent    On success, set to the parent directory vnode
 * @param name_out  On success, filled with the final filename component
 * @param name_max  Size of name_out buffer
 * @return 0 on success, negative on error
 */
int vfs_resolve_parent(const char *path, struct vnode **parent,
                       char *name_out, size_t name_max);

/* ========== Legacy ramfs helpers (in-tree dir ops) ========== */

struct vnode* vfs_lookup_ramfs(struct vnode *dir, const char *name);
struct vnode* vfs_resolve_path_ramfs(const char *path);
struct vnode* vfs_create_file(struct vnode *parent, const char *name);
struct vnode* vfs_mkdir_ramfs(struct vnode *parent, const char *name);
int vfs_add_dirent(struct vnode *dir, const char *name, struct vnode *child);

/* ========== Metadata Mutation Operations ========== */

/**
 * @brief Remove a file or empty directory from its parent directory.
 */
int vfs_unlink(struct vnode *parent, const char *name);

/**
 * @brief Rename (move) an entry within or across directories.
 */
int vfs_rename(struct vnode *old_parent, const char *old_name,
               struct vnode *new_parent, const char *new_name);

/**
 * @brief Change the permission mode bits of a vnode's inode.
 */
int vfs_chmod(struct vnode *vp, uint16_t mode);

/* ========== Permission Checking ========== */

/* Bit flags for vfs_check_perm() — mirrors POSIX access(2) */
#define VFS_R_OK  4   /* test for read permission    */
#define VFS_W_OK  2   /* test for write permission   */
#define VFS_X_OK  1   /* test for execute permission */

/**
 * @brief Check whether the calling process may access a vnode.
 */
int vfs_check_perm(struct vnode *vp, int how);

/**
 * @brief Like vfs_check_perm() but uses explicit credentials.
 */
int vfs_check_perm_as(struct vnode *vp, int how, uint32_t uid, uint32_t gid);
