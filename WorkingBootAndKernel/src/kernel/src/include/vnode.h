#pragma once
#include <stdint.h>
#include <stddef.h>

/* Maximum filename length for VFS directory entries */
#define VFS_NAME_MAX 255

enum vtype {
    VNON,   /* No type */
    VREG,   /* Regular file */
    VDIR,   /* Directory */
    VCHR,   /* Character device */
    VBLK    /* Block device */
};

/**
 * @brief VFS directory entry returned by readdir
 */
struct vfs_dirent {
    char     d_name[VFS_NAME_MAX + 1]; /* Null-terminated filename  */
    uint32_t d_type;                   /* Entry type (vtype values) */
    uint32_t d_size;                   /* File size in bytes        */
    uint32_t d_cluster;                /* Starting cluster (FS hint)*/
};

struct vnode {
    enum vtype v_type;          /* VREG, VDIR, VCHR, VBLK        */
    struct vnode_ops *v_op;     /* Operations dispatch table      */
    void *v_data;               /* FS-specific data (inode, etc.) */
    void *v_mount_data;         /* Per-mount FS state (fat32_mount, etc.) */
};

/**
 * @brief Vnode operations table
 *
 * All function pointers are optional (NULL = not supported).
 * Filesystem drivers fill in the operations they implement.
 */
struct vnode_ops {
    /* --- File / common operations --- */
    int (*vop_open)(struct vnode *vp);
    int (*vop_close)(struct vnode *vp);
    int (*vop_read)(struct vnode *vp, void *buf, size_t nbyte);
    int (*vop_write)(struct vnode *vp, void *buf, size_t nbyte);

    /* Offset-aware read/write (needed for real filesystems) */
    int (*vop_read_at)(struct vnode *vp, void *buf, size_t nbyte, uint64_t offset);
    int (*vop_write_at)(struct vnode *vp, const void *buf, size_t nbyte, uint64_t offset);

    /* --- Directory operations --- */

    /**
     * Look up a name inside a directory vnode.
     * On success, *result is set to the found child vnode.
     * @return 0 on success, negative on error (e.g. -ENOENT)
     */
    int (*vop_lookup)(struct vnode *dir, const char *name, struct vnode **result);

    /**
     * Read one directory entry at position *offset.
     * On success fills 'dent' and advances *offset.
     * @return 0 on success, 1 when no more entries, negative on error
     */
    int (*vop_readdir)(struct vnode *dir, struct vfs_dirent *dent, uint64_t *offset);

    /**
     * Create a new file inside a directory.
     * @return 0 on success (new vnode in *result), negative on error
     */
    int (*vop_create)(struct vnode *dir, const char *name, struct vnode **result);

    /**
     * Create a new subdirectory.
     * @return 0 on success (new vnode in *result), negative on error
     */
    int (*vop_mkdir)(struct vnode *dir, const char *name, struct vnode **result);

    /**
     * Remove a file or empty directory from a directory.
     * @return 0 on success, negative on error
     */
    int (*vop_remove)(struct vnode *dir, const char *name);

    /**
     * Get the size of a file/directory.
     * @return file size in bytes, or negative on error
     */
    int64_t (*vop_getsize)(struct vnode *vp);
};