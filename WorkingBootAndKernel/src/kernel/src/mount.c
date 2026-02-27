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
#include "include/spinlock.h"

/* ========== Global State ========== */

/* Registered filesystem types */
static struct filesystem *registered_fs = NULL;

/* Mount table */
static struct mount_table mounts = {0};

/* RAM block device for root filesystem */
static struct block_device *root_block_device = NULL;

/*
 * Filesystem-registry lock — protects registered_fs linked list.
 * Held during fs_register() and fs_find().
 */
static spinlock_t s_fs_list_lock = SPINLOCK_INIT;

/*
 * Mount-table lock — protects mounts (entries array, count, root pointer)
 * and root_block_device.
 * Held during do_mount(), do_unmount(), mount_lookup(), get_root_vnode().
 */
static spinlock_t s_mount_lock = SPINLOCK_INIT;

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

    uint64_t flags;
    spin_lock_irqsave(&s_fs_list_lock, &flags);

    /* Check for duplicate */
    struct filesystem *cur = registered_fs;
    while (cur) {
        if (strcmp(cur->name, fs->name) == 0) {
            spin_unlock_irqrestore(&s_fs_list_lock, &flags);
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

    spin_unlock_irqrestore(&s_fs_list_lock, &flags);

    uart_puts("[MOUNT] Registered filesystem: ");
    uart_puts(fs->name);
    uart_puts("\n");

    return 0;
}

struct filesystem *fs_find(const char *name) {
    uint64_t flags;
    spin_lock_irqsave(&s_fs_list_lock, &flags);
    struct filesystem *fs = registered_fs;
    while (fs) {
        if (strcmp(fs->name, name) == 0) {
            spin_unlock_irqrestore(&s_fs_list_lock, &flags);
            return fs;
        }
        fs = fs->next;
    }
    spin_unlock_irqrestore(&s_fs_list_lock, &flags);
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

    /* Find filesystem type — fs_find acquires s_fs_list_lock internally */
    struct filesystem *fs = fs_find(fs_name);
    if (!fs) {
        uart_puts("[MOUNT] Unknown filesystem: ");
        uart_puts(fs_name);
        uart_puts("\n");
        return -1;
    }

    /* Mount the filesystem outside the lock (may allocate) */
    struct vnode *root = fs->ops->mount(dev, flags);
    if (!root) {
        uart_puts("[MOUNT] Failed to mount ");
        uart_puts(fs_name);
        uart_puts(" at ");
        uart_puts(path);
        uart_puts("\n");
        return -1;
    }

    uint64_t lk_flags;
    spin_lock_irqsave(&s_mount_lock, &lk_flags);

    /* Find free mount entry */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts.entries[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spin_unlock_irqrestore(&s_mount_lock, &lk_flags);
        uart_puts("[MOUNT] Mount table full\n");
        if (fs->ops->unmount) fs->ops->unmount(root);
        return -1;
    }

    /* Check for duplicate mount point */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts.entries[i].in_use &&
            strcmp(mounts.entries[i].path, path) == 0) {
            spin_unlock_irqrestore(&s_mount_lock, &lk_flags);
            uart_puts("[MOUNT] Path already mounted: ");
            uart_puts(path);
            uart_puts("\n");
            if (fs->ops->unmount) fs->ops->unmount(root);
            return -1;
        }
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

    spin_unlock_irqrestore(&s_mount_lock, &lk_flags);

    uart_puts("[MOUNT] Mounted ");
    uart_puts(fs_name);
    uart_puts(" at ");
    uart_puts(path);
    uart_puts("\n");

    return 0;
}

int do_unmount(const char *path) {
    if (!path) return -1;

    uint64_t flags;
    spin_lock_irqsave(&s_mount_lock, &flags);

    /* Find mount entry */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts.entries[i].in_use &&
            strcmp(mounts.entries[i].path, path) == 0) {

            struct mount_entry *entry = &mounts.entries[i];

            /* Cannot unmount root */
            if (strcmp(path, "/") == 0) {
                spin_unlock_irqrestore(&s_mount_lock, &flags);
                uart_puts("[MOUNT] Cannot unmount root filesystem\n");
                return -1;
            }

            /* Grab what we need before clearing the entry */
            struct filesystem *fs   = entry->fs;
            struct vnode     *root  = entry->root;

            /* Clear entry while holding the lock */
            entry->in_use = 0;
            entry->root   = NULL;
            mounts.count--;

            spin_unlock_irqrestore(&s_mount_lock, &flags);

            /* Call filesystem unmount outside the lock */
            if (fs && fs->ops && fs->ops->unmount) {
                fs->ops->unmount(root);
            }

            uart_puts("[MOUNT] Unmounted ");
            uart_puts(path);
            uart_puts("\n");

            return 0;
        }
    }

    spin_unlock_irqrestore(&s_mount_lock, &flags);
    uart_puts("[MOUNT] Not mounted: ");
    uart_puts(path);
    uart_puts("\n");
    return -1;
}

struct mount_entry *mount_lookup(const char *path) {
    if (!path) return NULL;

    struct mount_entry *best = NULL;
    size_t best_len = 0;

    uint64_t flags;
    spin_lock_irqsave(&s_mount_lock, &flags);

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

    spin_unlock_irqrestore(&s_mount_lock, &flags);
    return best;
}

struct vnode *get_root_vnode(void) {
    uint64_t flags;
    spin_lock_irqsave(&s_mount_lock, &flags);
    struct vnode *root = mounts.root;
    spin_unlock_irqrestore(&s_mount_lock, &flags);
    return root;
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

/* Helper: create a text file and write content into it */
static struct vnode* create_text_file(struct vnode *parent, const char *name, const char *content) {
    struct vnode *f = vfs_create_file(parent, name);
    if (!f) {
        uart_puts("[FS POPULATE] WARN: Could not create ");
        uart_puts(name);
        uart_puts("\n");
        return NULL;
    }
    if (content) {
        size_t len = strlen(content);
        vfs_write(f, content, len);
    }
    return f;
}

int fs_populate_root(void) {
    uart_puts("[FS POPULATE] Building initial directory tree...\n");

    struct vnode *root = vfs_get_root();
    if (!root) {
        uart_puts("[FS POPULATE] ERROR: No root vnode\n");
        return -1;
    }

    /* ---- Top-level directories ---- */
    struct vnode *bin   = vfs_mkdir_ramfs(root, "bin");
    struct vnode *sbin  = vfs_mkdir_ramfs(root, "sbin");
    struct vnode *dev   = vfs_mkdir_ramfs(root, "dev");
    struct vnode *etc   = vfs_mkdir_ramfs(root, "etc");
    struct vnode *home  = vfs_mkdir_ramfs(root, "home");
    struct vnode *tmp   = vfs_mkdir_ramfs(root, "tmp");
    struct vnode *usr   = vfs_mkdir_ramfs(root, "usr");
    struct vnode *var   = vfs_mkdir_ramfs(root, "var");
    struct vnode *proc  = vfs_mkdir_ramfs(root, "proc");

    (void)sbin; (void)dev; (void)tmp; (void)proc;

    /* ---- /etc files ---- */
    if (etc) {
        create_text_file(etc, "passwd",
            "admin:x:0:0:System Administrator:/home/admin:/bin/shell\n"
            "guest:x:65534:65534:Guest Account:/tmp:/bin/shell\n");

        create_text_file(etc, "shadow",
            "admin:03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4\n"
            "guest:*\n");

        create_text_file(etc, "group",
            "root:x:0:admin\n"
            "users:x:100:\n"
            "nogroup:x:65534:guest\n");

        create_text_file(etc, "hostname", "tomahawk\n");

        create_text_file(etc, "motd",
            "\n"
            "  _____                    _                 _    \n"
            " |_   _|__  _ __ ___   __ _| |__   __ ___      _| | __\n"
            "   | |/ _ \\| '_ ` _ \\ / _` | '_ \\ / _` \\ \\ /\\ / / |/ /\n"
            "   | | (_) | | | | | | (_| | | | | (_| |\\ V  V /|   < \n"
            "   |_|\\___/|_| |_| |_|\\__,_|_| |_|\\__,_| \\_/\\_/ |_|\\_\\\n"
            "                                          OS v1.0\n"
            "\n"
            "Welcome to TomahawkOS! Type 'help' for available commands.\n\n");

        create_text_file(etc, "fstab",
            "# device  mountpoint  fstype  flags\n"
            "rootfs    /           ramfs   defaults\n"
            "none      /tmp        ramfs   noexec\n"
            "none      /dev        ramfs   noexec\n");

        /* init.conf is created separately by init_config_create_vfs_copy()
         * after init_config_load() parses the initrd, so it shows up in
         * ls /etc and is accessible via cat /etc/init.conf. */
    }

    /* ---- /home/admin ---- */
    if (home) {
        struct vnode *admin_home = vfs_mkdir_ramfs(home, "admin");
        if (admin_home) {
            create_text_file(admin_home, "welcome.txt",
                "Welcome to TomahawkOS, admin!\n"
                "This is your home directory.\n");
        }
    }

    /* ---- /usr sub-tree ---- */
    if (usr) {
        vfs_mkdir_ramfs(usr, "bin");
        vfs_mkdir_ramfs(usr, "lib");
        vfs_mkdir_ramfs(usr, "include");
    }

    /* ---- /var sub-tree ---- */
    if (var) {
        vfs_mkdir_ramfs(var, "log");
        vfs_mkdir_ramfs(var, "run");
    }

    /* ---- /bin initial files ---- */
    if (bin) {
        create_text_file(bin, "hello",
            "#!/bin/shell\necho Hello from TomahawkOS!\n");
    }

    uart_puts("[FS POPULATE] Directory tree created successfully\n");
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
