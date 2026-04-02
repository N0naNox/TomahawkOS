/**
 * @file mount.h
 * @brief Filesystem Mount Table and Root Filesystem
 * 
 * Provides mount point management for VFS.
 * Supports root filesystem and additional mount points.
 */

#ifndef MOUNT_H
#define MOUNT_H

#include <stdint.h>
#include <stddef.h>
#include "vnode.h"

/* Maximum number of mount points */
#define MAX_MOUNTS 16

/* Maximum path length (use vnode.h's definition if available) */
#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

/* Filesystem type identifiers */
#define FS_TYPE_RAMFS   1
#define FS_TYPE_EXT2    2   /* Future */
#define FS_TYPE_FAT32   3   /* Future */
#define FS_TYPE_DEVFS   4   /* Future - device filesystem */

/* Mount flags */
#define MNT_RDONLY      0x01  /* Read-only mount */
#define MNT_NOSUID      0x02  /* Ignore setuid bits */
#define MNT_NOEXEC      0x04  /* Disallow execution */
#define MNT_NODEV       0x08  /* Disallow device access */

/* Forward declarations */
struct block_device;
struct filesystem;

/**
 * @brief Filesystem type operations
 * 
 * Each filesystem type (ramfs, ext2, etc.) implements these.
 */
struct filesystem_ops {
    /* Mount the filesystem, return root vnode */
    struct vnode* (*mount)(struct block_device *dev, int flags);
    
    /* Unmount the filesystem */
    int (*unmount)(struct vnode *root);
    
    /* Sync filesystem metadata */
    int (*sync)(struct vnode *root);
    
    /* Get filesystem statistics */
    int (*statfs)(struct vnode *root, void *buf);
};

/**
 * @brief Registered filesystem type
 */
struct filesystem {
    char name[16];                  /* Filesystem name (e.g., "ramfs") */
    int type;                       /* FS_TYPE_* identifier */
    struct filesystem_ops *ops;     /* Filesystem operations */
    struct filesystem *next;        /* Linked list of registered fs types */
};

/**
 * @brief Mount point entry
 */
struct mount_entry {
    char path[MAX_PATH];            /* Mount path (e.g., "/", "/tmp") */
    struct vnode *root;             /* Root vnode of mounted filesystem */
    struct block_device *device;    /* Associated block device (NULL for ramfs) */
    struct filesystem *fs;          /* Filesystem type */
    int flags;                      /* Mount flags */
    int in_use;                     /* Entry is active */
};

/**
 * @brief Mount table
 */
struct mount_table {
    struct mount_entry entries[MAX_MOUNTS];
    int count;                      /* Number of active mounts */
    struct vnode *root;             /* Root vnode (/) */
};

/* ========== Filesystem Registration ========== */

/**
 * @brief Register a filesystem type
 * @param fs Filesystem to register
 * @return 0 on success, negative on error
 */
int fs_register(struct filesystem *fs);

/**
 * @brief Find a filesystem type by name
 * @param name Filesystem name
 * @return Filesystem pointer or NULL
 */
struct filesystem *fs_find(const char *name);

/* ========== Mount Operations ========== */

/**
 * @brief Initialize the mount subsystem
 */
void mount_init(void);

/**
 * @brief Mount a filesystem
 * @param path Mount path
 * @param fs_name Filesystem type name
 * @param dev Block device (NULL for memory filesystems)
 * @param flags Mount flags
 * @return 0 on success, negative on error
 */
int do_mount(const char *path, const char *fs_name, 
             struct block_device *dev, int flags);

/**
 * @brief Unmount a filesystem
 * @param path Mount path
 * @return 0 on success, negative on error
 */
int do_unmount(const char *path);

/**
 * @brief Get mount entry for a path
 * @param path Path to look up
 * @return Mount entry or NULL
 */
struct mount_entry *mount_lookup(const char *path);

/**
 * @brief Get the root vnode
 * @return Root vnode
 */
struct vnode *get_root_vnode(void);

/* ========== Boot Filesystem Initialization ========== */

/**
 * @brief Initialize and mount root filesystem during boot
 * 
 * This is called early in kernel boot to set up the initial
 * filesystem hierarchy. Currently uses ramfs, but can be
 * extended to detect and mount physical root filesystems.
 * 
 * @return 0 on success, negative on error
 */
int fs_init_root(void);

/**
 * @brief Mount standard system directories
 * 
 * Creates standard mount points like /dev, /tmp, /proc
 * 
 * @return 0 on success, negative on error
 */
int fs_mount_system_dirs(void);

/**
 * @brief Populate the root filesystem with standard directories and files
 * 
 * Creates /bin, /sbin, /dev, /etc, /home, /tmp, /usr, /var, /proc
 * and populates /etc with passwd, hostname, motd files.
 * 
 * @return 0 on success, negative on error
 */
int fs_populate_root(void);

/**
 * @brief Print mount table (for debugging)
 */
void mount_print_table(void);

#endif /* MOUNT_H */
