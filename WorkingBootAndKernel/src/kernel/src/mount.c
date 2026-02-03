/**
 * @file mount.c
 * @brief Filesystem Mount Table and Root Initialization
 * 
 * Implements:
 * - Filesystem type registration
 * - Mount table management
 * - Root filesystem initialization during boot
 */

#include "include/mount.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/block_device.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include "include/uart.h"

/* ========== Global State ========== */

/* Registered filesystem types */
static struct filesystem *registered_fs = NULL;

/* Mount table */
static struct mount_table mounts = {0};

/* RAM block device for root filesystem */
static struct block_device *root_block_device = NULL;

/* ========== RAMFS Implementation ========== */

/* Forward declaration - ramfs mount creates root vnode */
static struct vnode* ramfs_mount(struct block_device *dev, int flags);
static int ramfs_unmount(struct vnode *root);
static int ramfs_sync(struct vnode *root);

static struct filesystem_ops ramfs_fs_ops = {
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
    .sync = ramfs_sync,
    .statfs = NULL
};

static struct filesystem ramfs_fs = {
    .name = "ramfs",
    .type = FS_TYPE_RAMFS,
    .ops = &ramfs_fs_ops,
    .next = NULL
};

static struct vnode* ramfs_mount(struct block_device *dev, int flags) {
    (void)dev;   /* ramfs doesn't need a block device */
    (void)flags;
    
    /* Create root directory vnode for this ramfs instance */
    struct vnode *root = vfs_create_vnode(VDIR);
    if (!root) {
        uart_puts("[RAMFS] Failed to create root vnode\n");
        return NULL;
    }
    
    uart_puts("[RAMFS] Mounted ramfs instance\n");
    return root;
}

static int ramfs_unmount(struct vnode *root) {
    if (root) {
        vfs_free_vnode(root);
    }
    return 0;
}

static int ramfs_sync(struct vnode *root) {
    /* RAM filesystem - nothing to sync */
    (void)root;
    return 0;
}

/* ========== Filesystem Registration ========== */

int fs_register(struct filesystem *fs) {
    if (!fs || !fs->ops) {
        return -1;
    }
    
    /* Check for duplicate */
    struct filesystem *cur = registered_fs;
    while (cur) {
        if (strcmp(cur->name, fs->name) == 0) {
            uart_puts("[MOUNT] Filesystem already registered: ");
            uart_puts(fs->name);
            uart_puts("\n");
            return -1;
        }
        cur = cur->next;
    }
    
    /* Add to list */
    fs->next = registered_fs;
    registered_fs = fs;
    
    uart_puts("[MOUNT] Registered filesystem: ");
    uart_puts(fs->name);
    uart_puts("\n");
    
    return 0;
}

struct filesystem *fs_find(const char *name) {
    struct filesystem *fs = registered_fs;
    while (fs) {
        if (strcmp(fs->name, name) == 0) {
            return fs;
        }
        fs = fs->next;
    }
    return NULL;
}

/* ========== Mount Operations ========== */

void mount_init(void) {
    /* Clear mount table */
    memset(&mounts, 0, sizeof(mounts));
    
    /* Register built-in filesystem types */
    fs_register(&ramfs_fs);
    
    uart_puts("[MOUNT] Mount subsystem initialized\n");
}

int do_mount(const char *path, const char *fs_name, 
             struct block_device *dev, int flags) {
    if (!path || !fs_name) {
        return -1;
    }
    
    /* Find filesystem type */
    struct filesystem *fs = fs_find(fs_name);
    if (!fs) {
        uart_puts("[MOUNT] Unknown filesystem: ");
        uart_puts(fs_name);
        uart_puts("\n");
        return -1;
    }
    
    /* Find free mount entry */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts.entries[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        uart_puts("[MOUNT] Mount table full\n");
        return -1;
    }
    
    /* Check for duplicate mount point */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts.entries[i].in_use && 
            strcmp(mounts.entries[i].path, path) == 0) {
            uart_puts("[MOUNT] Path already mounted: ");
            uart_puts(path);
            uart_puts("\n");
            return -1;
        }
    }
    
    /* Mount the filesystem */
    struct vnode *root = fs->ops->mount(dev, flags);
    if (!root) {
        uart_puts("[MOUNT] Failed to mount ");
        uart_puts(fs_name);
        uart_puts(" at ");
        uart_puts(path);
        uart_puts("\n");
        return -1;
    }
    
    /* Fill mount entry */
    struct mount_entry *entry = &mounts.entries[slot];
    memset(entry->path, 0, MAX_PATH);
    size_t path_len = strlen(path);
    if (path_len >= MAX_PATH) path_len = MAX_PATH - 1;
    memcpy(entry->path, path, path_len);
    
    entry->root = root;
    entry->device = dev;
    entry->fs = fs;
    entry->flags = flags;
    entry->in_use = 1;
    mounts.count++;
    
    /* If mounting root, save it */
    if (strcmp(path, "/") == 0) {
        mounts.root = root;
    }
    
    uart_puts("[MOUNT] Mounted ");
    uart_puts(fs_name);
    uart_puts(" at ");
    uart_puts(path);
    uart_puts("\n");
    
    return 0;
}

int do_unmount(const char *path) {
    if (!path) return -1;
    
    /* Find mount entry */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts.entries[i].in_use &&
            strcmp(mounts.entries[i].path, path) == 0) {
            
            struct mount_entry *entry = &mounts.entries[i];
            
            /* Cannot unmount root */
            if (strcmp(path, "/") == 0) {
                uart_puts("[MOUNT] Cannot unmount root filesystem\n");
                return -1;
            }
            
            /* Call filesystem unmount */
            if (entry->fs && entry->fs->ops && entry->fs->ops->unmount) {
                entry->fs->ops->unmount(entry->root);
            }
            
            /* Clear entry */
            entry->in_use = 0;
            entry->root = NULL;
            mounts.count--;
            
            uart_puts("[MOUNT] Unmounted ");
            uart_puts(path);
            uart_puts("\n");
            
            return 0;
        }
    }
    
    uart_puts("[MOUNT] Not mounted: ");
    uart_puts(path);
    uart_puts("\n");
    return -1;
}

struct mount_entry *mount_lookup(const char *path) {
    if (!path) return NULL;
    
    struct mount_entry *best = NULL;
    size_t best_len = 0;
    
    /* Find longest matching mount point */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts.entries[i].in_use) continue;
        
        const char *mp = mounts.entries[i].path;
        size_t mp_len = strlen(mp);
        
        /* Check if path starts with mount point */
        int match = 1;
        for (size_t j = 0; j < mp_len; j++) {
            if (path[j] != mp[j]) {
                match = 0;
                break;
            }
        }
        
        /* Root always matches, others need path separator or exact match */
        if (match) {
            if (mp_len == 1 && mp[0] == '/') {
                /* Root matches everything */
                if (!best || mp_len > best_len) {
                    best = &mounts.entries[i];
                    best_len = mp_len;
                }
            } else if (path[mp_len] == '/' || path[mp_len] == '\0') {
                if (mp_len > best_len) {
                    best = &mounts.entries[i];
                    best_len = mp_len;
                }
            }
        }
    }
    
    return best;
}

struct vnode *get_root_vnode(void) {
    return mounts.root;
}

/* ========== Boot Filesystem Initialization ========== */

int fs_init_root(void) {
    uart_puts("\n[FS INIT] Initializing filesystem subsystem...\n");
    
    /* Initialize mount subsystem */
    mount_init();
    
    /* Initialize block device subsystem */
    block_device_init();
    
    /* Create RAM block device for root filesystem */
    uart_puts("[FS INIT] Creating root RAM block device...\n");
    root_block_device = ramblock_create("rootfs", 256);  /* 1MB root fs */
    if (!root_block_device) {
        uart_puts("[FS INIT] ERROR: Failed to create root block device\n");
        return -1;
    }
    block_device_register(root_block_device);
    
    /* Set VFS backing device BEFORE initializing VFS */
    vfs_set_backing_device(root_block_device);
    
    /* Initialize VFS layer */
    vfs_init();
    
    /* Mount root filesystem (ramfs for now) */
    uart_puts("[FS INIT] Mounting root filesystem...\n");
    if (do_mount("/", "ramfs", root_block_device, 0) != 0) {
        uart_puts("[FS INIT] ERROR: Failed to mount root filesystem\n");
        return -1;
    }
    
    uart_puts("[FS INIT] Root filesystem mounted successfully!\n");
    return 0;
}

int fs_mount_system_dirs(void) {
    uart_puts("[FS INIT] Creating system directories...\n");
    
    /* Mount /tmp as separate ramfs */
    if (do_mount("/tmp", "ramfs", NULL, 0) != 0) {
        uart_puts("[FS INIT] Warning: Failed to mount /tmp\n");
    }
    
    /* Mount /dev as ramfs (future: devfs) */
    if (do_mount("/dev", "ramfs", NULL, MNT_NOEXEC) != 0) {
        uart_puts("[FS INIT] Warning: Failed to mount /dev\n");
    }
    
    uart_puts("[FS INIT] System directories mounted\n");
    return 0;
}

void mount_print_table(void) {
    uart_puts("\n=== Mount Table ===\n");
    uart_puts("Path            Filesystem  Device\n");
    uart_puts("--------------- ----------- --------\n");
    
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts.entries[i].in_use) continue;
        
        struct mount_entry *e = &mounts.entries[i];
        
        /* Print path (padded to 15 chars) */
        uart_puts(e->path);
        size_t len = strlen(e->path);
        for (size_t j = len; j < 16; j++) uart_puts(" ");
        
        /* Print filesystem type */
        if (e->fs) {
            uart_puts(e->fs->name);
            len = strlen(e->fs->name);
            for (size_t j = len; j < 12; j++) uart_puts(" ");
        } else {
            uart_puts("unknown     ");
        }
        
        /* Print device */
        if (e->device) {
            uart_puts(e->device->name);
        } else {
            uart_puts("none");
        }
        
        uart_puts("\n");
    }
    uart_puts("===================\n\n");
}
