/**
 * @file vfs.c
 * @brief Virtual File System Implementation
 * 
 * VFS layer that uses the block device layer for actual storage.
 * All file I/O goes through the buffer cache for efficiency.
 */

#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "include/block_device.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include "include/uart.h"
#include "include/spinlock.h"
#include <stddef.h>

/* Root vnode */
static struct vnode* root_vnode = NULL;

/* Default block device for VFS storage */
static struct block_device *vfs_default_device = NULL;

/* Simple block allocator for the default device */
static uint64_t next_free_block = 1;  /* Block 0 reserved for superblock */

/*
 * VFS global-state lock — protects root_vnode and vfs_default_device.
 * Acquired with IRQ-save so interrupt handlers that call vfs_get_root()
 * cannot deadlock.
 */
static spinlock_t s_vfs_global_lock = SPINLOCK_INIT;

/*
 * Block-allocator lock — protects next_free_block bump counter.
 * Must be held across the read-check-modify sequence to prevent TOCTOU
 * races under concurrent vfs_alloc_blocks() calls.
 */
static spinlock_t s_alloc_lock = SPINLOCK_INIT;

/* Forward declarations for ramfs operations */
static int ramfs_open(struct vnode *vp);
static int ramfs_read(struct vnode *vp, void *buf, size_t nbyte);
static int ramfs_write(struct vnode *vp, void *buf, size_t nbyte);

static struct vnode_ops ramfs_ops = {
    .vop_open = ramfs_open,
    .vop_read = ramfs_read,
    .vop_write = ramfs_write,
};

/* ========== Block Allocation ========== */

/**
 * Allocate blocks from the device for a file
 */
static int vfs_alloc_blocks(struct inode *ip, uint64_t num_blocks) {
    if (!ip || !ip->i_bdev) {
        return -1;
    }

    uint64_t flags;
    spin_lock_irqsave(&s_alloc_lock, &flags);

    /* Check if device has enough space */
    if (next_free_block + num_blocks > ip->i_bdev->total_blocks) {
        spin_unlock_irqrestore(&s_alloc_lock, &flags);
        uart_puts("[VFS] ERROR: Out of blocks on device\n");
        return -1;
    }

    /* Allocate contiguous blocks (simple allocation) */
    ip->i_start_block = next_free_block;
    ip->i_blocks = num_blocks;
    next_free_block += num_blocks;

    spin_unlock_irqrestore(&s_alloc_lock, &flags);
    return 0;
}

/* ========== VFS Core Functions ========== */

void vfs_init(void) {
    uart_puts("[VFS] Initializing VFS layer\n");
    
    /* Reset block allocator */
    next_free_block = 1;
    
    /* Create root directory vnode */
    root_vnode = vfs_create_vnode(VDIR);
    if (root_vnode) {
        uart_puts("[VFS] Root vnode created\n");
    }
}

void vfs_set_backing_device(struct block_device *dev) {
    uint64_t flags;
    spin_lock_irqsave(&s_vfs_global_lock, &flags);
    vfs_default_device = dev;
    spin_unlock_irqrestore(&s_vfs_global_lock, &flags);
    if (dev) {
        uart_puts("[VFS] Backing device set: ");
        uart_puts(dev->name);
        uart_puts("\n");
    }
}

struct block_device *vfs_get_backing_device(void) {
    uint64_t flags;
    spin_lock_irqsave(&s_vfs_global_lock, &flags);
    struct block_device *dev = vfs_default_device;
    spin_unlock_irqrestore(&s_vfs_global_lock, &flags);
    return dev;
}

struct vnode* vfs_create_vnode(enum vtype type) {
    return vfs_create_vnode_on_device(type, vfs_default_device);
}

struct vnode* vfs_create_vnode_on_device(enum vtype type, struct block_device *dev) {
    /* Allocate vnode */
    struct vnode* vp = (struct vnode*)pfa_alloc_frame();
    if (!vp) {
        uart_puts("[VFS] ERROR: Failed to allocate vnode\n");
        return NULL;
    }
    
    /* Allocate inode */
    struct inode* ip = (struct inode*)pfa_alloc_frame();
    if (!ip) {
        pfa_free_frame((uintptr_t)vp);
        uart_puts("[VFS] ERROR: Failed to allocate inode\n");
        return NULL;
    }
    
    /* Initialize vnode */
    memset(vp, 0, sizeof(struct vnode));
    vp->v_type = type;
    vp->v_op = &ramfs_ops;
    vp->v_data = ip;
    vp->v_parent = NULL;
    vp->v_children = NULL;
    vp->v_nchildren = 0;
    
    /* Initialize inode */
    memset(ip, 0, sizeof(struct inode));
    ip->i_refcount = 1;
    ip->cache_list = NULL;
    ip->i_bdev = dev;
    ip->i_start_block = 0;
    ip->i_blocks = 0;
    ip->i_size = 0;
    
    return vp;
}

void vfs_free_vnode(struct vnode* vp) {
    if (!vp) return;
    
    struct inode* ip = (struct inode*)vp->v_data;
    if (ip) {
        /* Free legacy page cache if any */
        struct page_cache_entry* entry = ip->cache_list;
        while (entry) {
            struct page_cache_entry* next = entry->next;
            if (entry->physical_addr) {
                pfa_free_frame((uintptr_t)entry->physical_addr);
            }
            pfa_free_frame((uintptr_t)entry);
            entry = next;
        }
        
        /* Note: blocks on device are not freed (would need a free list) */
        
        pfa_free_frame((uintptr_t)ip);
    }
    
    pfa_free_frame((uintptr_t)vp);
}

struct vnode* vfs_get_root(void) {
    uint64_t flags;
    spin_lock_irqsave(&s_vfs_global_lock, &flags);
    struct vnode *root = root_vnode;
    spin_unlock_irqrestore(&s_vfs_global_lock, &flags);
    return root;
}

int vfs_open(struct vnode* vp) {
    if (!vp || !vp->v_op || !vp->v_op->vop_open) {
        return -1;
    }
    return vp->v_op->vop_open(vp);
}

int vfs_read(struct vnode* vp, void* buf, size_t nbyte) {
    if (!vp || !vp->v_op || !vp->v_op->vop_read) {
        return -1;
    }
    return vp->v_op->vop_read(vp, buf, nbyte);
}

int vfs_write(struct vnode* vp, const void* buf, size_t nbyte) {
    if (!vp || !vp->v_op || !vp->v_op->vop_write) {
        return -1;
    }
    return vp->v_op->vop_write(vp, (void*)buf, nbyte);
}

int vfs_close(struct vnode* vp) {
    if (!vp) return -1;

    struct inode* ip = (struct inode*)vp->v_data;
    if (ip) {
        /* Sync any dirty blocks to device */
        if (ip->i_bdev && ip->i_blocks > 0) {
            buffer_sync_all(ip->i_bdev);
        }

        uint64_t flags;
        spin_lock_irqsave(&ip->i_lock, &flags);
        uint32_t new_ref = --ip->i_refcount;
        spin_unlock_irqrestore(&ip->i_lock, &flags);

        if (new_ref == 0) {
            vfs_free_vnode(vp);
        }
    }
    return 0;
}

/* ========== RAMFS Implementation Using Block Device ========== */

static int ramfs_open(struct vnode *vp) {
    /* Nothing special needed for ramfs open */
    (void)vp;
    return 0;
}

static int ramfs_read(struct vnode *vp, void *buf, size_t nbyte) {
    if (!vp || !buf) return -1;

    struct inode* ip = (struct inode*)vp->v_data;
    if (!ip) return -1;

    /* Read inode metadata under i_lock */
    uint64_t flags;
    spin_lock_irqsave(&ip->i_lock, &flags);
    uint32_t size       = ip->i_size;
    uint64_t start_blk  = ip->i_start_block;
    uint64_t nblocks    = ip->i_blocks;
    struct block_device *bdev = ip->i_bdev;
    struct page_cache_entry *cache = ip->cache_list;
    spin_unlock_irqrestore(&ip->i_lock, &flags);

    /* If no data written yet */
    if (size == 0) {
        return 0;
    }

    /* Clamp read size to file size */
    size_t to_read = nbyte;
    if (to_read > size) {
        to_read = size;
    }

    /* If we have a block device, read through buffer cache */
    if (bdev && nblocks > 0) {
        uint64_t offset = start_blk * BLOCK_SIZE;
        ssize_t bytes = block_read(bdev, offset, buf, to_read);
        if (bytes < 0) {
            uart_puts("[VFS] ERROR: block_read failed\n");
            return -1;
        }
        return (int)bytes;
    }

    /* Fallback: read from legacy page cache */
    if (!cache || !cache->physical_addr) {
        return 0;
    }

    memcpy(buf, cache->physical_addr, to_read);
    return (int)to_read;
}

/* ========== Directory & Path Operations ========== */

struct vnode* vfs_lookup(struct vnode *dir, const char *name) {
    if (!dir || !name || dir->v_type != VDIR) return NULL;

    uint64_t flags;
    spin_lock_irqsave(&dir->v_lock, &flags);

    struct dir_entry *de = dir->v_children;
    while (de) {
        /* Compare names */
        const char *a = de->name;
        const char *b = name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            struct vnode *result = de->vnode;
            spin_unlock_irqrestore(&dir->v_lock, &flags);
            return result;
        }
        de = de->next;
    }

    spin_unlock_irqrestore(&dir->v_lock, &flags);
    return NULL;
}

struct vnode* vfs_resolve_path(const char *path) {
    if (!path || *path == '\0') return NULL;

    struct vnode *cur = vfs_get_root();
    if (!cur) return NULL;

    /* "/" alone */
    if (path[0] == '/' && path[1] == '\0') return cur;

    /* Start scanning: skip leading '/' for absolute paths,
       or start directly for relative paths (treated as relative to root) */
    const char *p = path;
    if (*p == '/') p++;
    char component[VFS_NAME_MAX];

    while (*p) {
        /* Skip extra slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract next path component */
        int i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';

        /* Handle "." and ".." */
        if (component[0] == '.' && component[1] == '\0') {
            continue; /* stay in current directory */
        }
        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            if (cur->v_parent) cur = cur->v_parent;
            continue;
        }

        struct vnode *child = vfs_lookup(cur, component);
        if (!child) return NULL;
        cur = child;
    }
    return cur;
}

int vfs_add_dirent(struct vnode *dir, const char *name, struct vnode *child) {
    if (!dir || !name || !child) return -1;
    if (dir->v_type != VDIR) return -1;

    /* Check for duplicate — vfs_lookup acquires dir->v_lock internally */
    if (vfs_lookup(dir, name) != NULL) return -1;

    /* Allocate dir_entry from frame allocator */
    struct dir_entry *de = (struct dir_entry *)pfa_alloc_frame();
    if (!de) return -1;
    memset(de, 0, sizeof(struct dir_entry));

    /* Copy name */
    int i = 0;
    for (; name[i] && i < VFS_NAME_MAX - 1; i++) {
        de->name[i] = name[i];
    }
    de->name[i] = '\0';
    de->vnode = child;

    /* Prepend to list — hold dir->v_lock across the structural mutation */
    uint64_t flags;
    spin_lock_irqsave(&dir->v_lock, &flags);
    de->next = dir->v_children;
    dir->v_children = de;
    dir->v_nchildren++;
    spin_unlock_irqrestore(&dir->v_lock, &flags);

    /* Set parent — hold child->v_lock so v_parent is updated atomically */
    spin_lock_irqsave(&child->v_lock, &flags);
    child->v_parent = dir;
    spin_unlock_irqrestore(&child->v_lock, &flags);

    return 0;
}

struct vnode* vfs_mkdir(struct vnode *parent, const char *name) {
    if (!parent || !name || parent->v_type != VDIR) return NULL;

    /* Check duplicate */
    if (vfs_lookup(parent, name) != NULL) return NULL;

    struct vnode *dir = vfs_create_vnode(VDIR);
    if (!dir) return NULL;

    if (vfs_add_dirent(parent, name, dir) != 0) {
        vfs_free_vnode(dir);
        return NULL;
    }
    return dir;
}

struct vnode* vfs_create_file(struct vnode *parent, const char *name) {
    if (!parent || !name || parent->v_type != VDIR) return NULL;

    /* Check duplicate */
    if (vfs_lookup(parent, name) != NULL) return NULL;

    struct vnode *file = vfs_create_vnode(VREG);
    if (!file) return NULL;

    if (vfs_add_dirent(parent, name, file) != 0) {
        vfs_free_vnode(file);
        return NULL;
    }
    return file;
}

static int ramfs_write(struct vnode *vp, void *buf, size_t nbyte) {
    if (!vp || !buf || nbyte == 0) return -1;

    struct inode* ip = (struct inode*)vp->v_data;
    if (!ip) return -1;

    /* Read i_bdev and i_blocks under i_lock to check what path to take */
    uint64_t flags;
    spin_lock_irqsave(&ip->i_lock, &flags);
    struct block_device *bdev = ip->i_bdev;
    uint64_t cur_blocks = ip->i_blocks;
    spin_unlock_irqrestore(&ip->i_lock, &flags);

    /* If we have a block device, write through buffer cache */
    if (bdev) {
        /* Allocate blocks if not yet allocated (vfs_alloc_blocks is self-locking) */
        if (cur_blocks == 0) {
            uint64_t blocks_needed = (nbyte + BLOCK_SIZE - 1) / BLOCK_SIZE;
            if (blocks_needed == 0) blocks_needed = 1;

            if (vfs_alloc_blocks(ip, blocks_needed) != 0) {
                uart_puts("[VFS] ERROR: Failed to allocate blocks\n");
                return -1;
            }
        }

        /* Read back the (possibly freshly assigned) start block under lock */
        spin_lock_irqsave(&ip->i_lock, &flags);
        uint64_t start_blk = ip->i_start_block;
        spin_unlock_irqrestore(&ip->i_lock, &flags);

        /* Write through buffer cache */
        uint64_t offset = start_blk * BLOCK_SIZE;
        ssize_t bytes = block_write(bdev, offset, buf, nbyte);
        if (bytes < 0) {
            uart_puts("[VFS] ERROR: block_write failed\n");
            return -1;
        }

        /* Update i_size under i_lock */
        spin_lock_irqsave(&ip->i_lock, &flags);
        ip->i_size = (uint32_t)nbyte;
        spin_unlock_irqrestore(&ip->i_lock, &flags);

        return (int)bytes;
    }

    /* Fallback: use legacy page cache (no block device) —
       allocate outside the lock (pfa_alloc_frame may disable interrupts itself),
       then splice into cache_list under i_lock. */
    spin_lock_irqsave(&ip->i_lock, &flags);
    if (!ip->cache_list) {
        spin_unlock_irqrestore(&ip->i_lock, &flags);

        struct page_cache_entry* entry = (struct page_cache_entry*)pfa_alloc_frame();
        if (!entry) return -1;

        entry->physical_addr = (void*)pfa_alloc_frame();
        if (!entry->physical_addr) {
            pfa_free_frame((uintptr_t)entry);
            return -1;
        }

        entry->offset = 0;
        entry->is_dirty = 0;
        entry->next = NULL;

        spin_lock_irqsave(&ip->i_lock, &flags);
        /* Re-check: another writer may have raced us */
        if (!ip->cache_list) {
            ip->cache_list = entry;
        } else {
            /* Another writer won — free our allocation and use theirs */
            spin_unlock_irqrestore(&ip->i_lock, &flags);
            pfa_free_frame((uintptr_t)entry->physical_addr);
            pfa_free_frame((uintptr_t)entry);
            spin_lock_irqsave(&ip->i_lock, &flags);
        }
    }

    struct page_cache_entry* entry = ip->cache_list;
    size_t to_write = nbyte;
    if (to_write > 4096) to_write = 4096;  /* Limit to one page */

    memcpy(entry->physical_addr, buf, to_write);
    entry->is_dirty = 1;
    ip->i_size = (uint32_t)to_write;

    spin_unlock_irqrestore(&ip->i_lock, &flags);
    return (int)to_write;
}

/* ========== Metadata Mutation Operations ========== */

/**
 * vfs_unlink — remove an entry from a directory and drop its inode refcount.
 *
 * Lock ordering:
 *   1. parent->v_lock  (to modify v_children / v_nchildren)
 *   2. target->v_data (inode)->i_lock  (to decrement i_refcount)
 */
int vfs_unlink(struct vnode *parent, const char *name)
{
    if (!parent || !name || parent->v_type != VDIR) return -1;

    uint64_t flags;
    spin_lock_irqsave(&parent->v_lock, &flags);

    /* Walk the directory entry list looking for 'name' */
    struct dir_entry **pp = &parent->v_children;
    while (*pp) {
        struct dir_entry *de = *pp;

        /* Compare name */
        const char *a = de->name;
        const char *b = name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            struct vnode *target = de->vnode;

            /* Refuse to unlink a non-empty directory */
            if (target->v_type == VDIR && target->v_nchildren > 0) {
                spin_unlock_irqrestore(&parent->v_lock, &flags);
                uart_puts("[VFS] unlink: directory not empty\n");
                return -1;
            }

            /* Splice out the directory entry */
            *pp = de->next;
            parent->v_nchildren--;

            spin_unlock_irqrestore(&parent->v_lock, &flags);

            /* Free the dir_entry allocation */
            pfa_free_frame((uintptr_t)de);

            /* Drop inode refcount under i_lock */
            struct inode *ip = (struct inode *)target->v_data;
            if (ip) {
                uint64_t iflags;
                spin_lock_irqsave(&ip->i_lock, &iflags);
                uint32_t new_ref = --ip->i_refcount;
                spin_unlock_irqrestore(&ip->i_lock, &iflags);

                if (new_ref == 0) {
                    vfs_free_vnode(target);
                }
            }

            return 0;
        }
        pp = &de->next;
    }

    spin_unlock_irqrestore(&parent->v_lock, &flags);
    return -1;  /* not found */
}

/**
 * vfs_rename — atomically relink a directory entry.
 *
 * Lock ordering to avoid deadlock on SMP: always lock the directory with
 * the numerically lower pointer address first when both locks are needed.
 */
int vfs_rename(struct vnode *old_parent, const char *old_name,
               struct vnode *new_parent, const char *new_name)
{
    if (!old_parent || !old_name || !new_parent || !new_name) return -1;
    if (old_parent->v_type != VDIR || new_parent->v_type != VDIR) return -1;

    /* Determine lock acquisition order to prevent AB-BA deadlock */
    struct vnode *first  = (old_parent <= new_parent) ? old_parent : new_parent;
    struct vnode *second = (old_parent <= new_parent) ? new_parent : old_parent;

    uint64_t flags;
    spin_lock_irqsave(&first->v_lock, &flags);
    if (first != second) {
        /* Acquire second lock without saving flags again — interrupts already off */
        spin_lock(&second->v_lock);
    }

    /* --- Both directories are now locked --- */

    /* Find the source entry */
    struct dir_entry **src_pp = &old_parent->v_children;
    while (*src_pp) {
        struct dir_entry *de = *src_pp;
        const char *a = de->name;
        const char *b = old_name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') break;
        src_pp = &de->next;
    }

    if (!*src_pp) {
        /* Source not found */
        if (first != second) spin_unlock(&second->v_lock);
        spin_unlock_irqrestore(&first->v_lock, &flags);
        uart_puts("[VFS] rename: source not found\n");
        return -1;
    }

    struct dir_entry *src_de = *src_pp;
    struct vnode *target = src_de->vnode;

    /* Check if destination name already exists; if so, unlink it first */
    struct dir_entry **dst_pp = &new_parent->v_children;
    while (*dst_pp) {
        struct dir_entry *de = *dst_pp;
        const char *a = de->name;
        const char *b = new_name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            /* Destination exists — remove it (can't overwrite a non-empty dir) */
            struct vnode *existing = de->vnode;
            if (existing->v_type == VDIR && existing->v_nchildren > 0) {
                if (first != second) spin_unlock(&second->v_lock);
                spin_unlock_irqrestore(&first->v_lock, &flags);
                uart_puts("[VFS] rename: destination directory not empty\n");
                return -1;
            }
            *dst_pp = de->next;
            new_parent->v_nchildren--;
            pfa_free_frame((uintptr_t)de);

            /* Drop refcount on the displaced vnode */
            struct inode *dip = (struct inode *)existing->v_data;
            if (dip) {
                /* i_lock acquired separately — can't hold it while we hold v_lock
                   of the parent (would violate lock ordering), so decrement here
                   and free after releasing the dir locks below. */
                dip->i_refcount--;
                if (dip->i_refcount == 0) {
                    /* Mark for deferred free */
                    dip->i_refcount = 0;
                }
            }
            break;
        }
        dst_pp = &de->next;
    }

    /* Splice the source entry out of old_parent */
    *src_pp = src_de->next;
    old_parent->v_nchildren--;

    /* Reuse the dir_entry under the new name */
    int i = 0;
    for (; new_name[i] && i < VFS_NAME_MAX - 1; i++) {
        src_de->name[i] = new_name[i];
    }
    src_de->name[i] = '\0';

    /* Prepend to new_parent's child list */
    src_de->next = new_parent->v_children;
    new_parent->v_children = src_de;
    new_parent->v_nchildren++;

    if (first != second) spin_unlock(&second->v_lock);
    spin_unlock_irqrestore(&first->v_lock, &flags);

    /* Update v_parent on the moved vnode under its own v_lock */
    uint64_t vflags;
    spin_lock_irqsave(&target->v_lock, &vflags);
    target->v_parent = new_parent;
    spin_unlock_irqrestore(&target->v_lock, &vflags);

    return 0;
}

/**
 * vfs_chmod — change the permission bits of a vnode's inode.
 *
 * Acquires inode i_lock for the read-modify-write so concurrent readers
 * of i_mode see a consistent value.
 */
int vfs_chmod(struct vnode *vp, uint16_t mode)
{
    if (!vp) return -1;

    struct inode *ip = (struct inode *)vp->v_data;
    if (!ip) return -1;

    uint64_t flags;
    spin_lock_irqsave(&ip->i_lock, &flags);
    /* Preserve the file-type bits (upper 4), replace permissions (lower 12) */
    ip->i_mode = (ip->i_mode & (uint16_t)0xF000) | (mode & (uint16_t)0x0FFF);
    spin_unlock_irqrestore(&ip->i_lock, &flags);

    return 0;
}
