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
#include <stddef.h>

/* Root vnode */
static struct vnode* root_vnode = NULL;

/* Default block device for VFS storage */
static struct block_device *vfs_default_device = NULL;

/* Simple block allocator for the default device */
static uint64_t next_free_block = 1;  /* Block 0 reserved for superblock */

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
    
    /* Check if device has enough space */
    if (next_free_block + num_blocks > ip->i_bdev->total_blocks) {
        uart_puts("[VFS] ERROR: Out of blocks on device\n");
        return -1;
    }
    
    /* Allocate contiguous blocks (simple allocation) */
    ip->i_start_block = next_free_block;
    ip->i_blocks = num_blocks;
    next_free_block += num_blocks;
    
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
    vfs_default_device = dev;
    if (dev) {
        uart_puts("[VFS] Backing device set: ");
        uart_puts(dev->name);
        uart_puts("\n");
    }
}

struct block_device *vfs_get_backing_device(void) {
    return vfs_default_device;
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
    return root_vnode;
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
        
        ip->i_refcount--;
        if (ip->i_refcount == 0) {
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
    
    /* If no data written yet */
    if (ip->i_size == 0) {
        return 0;
    }
    
    /* Clamp read size to file size */
    size_t to_read = nbyte;
    if (to_read > ip->i_size) {
        to_read = ip->i_size;
    }
    
    /* If we have a block device, read through buffer cache */
    if (ip->i_bdev && ip->i_blocks > 0) {
        uint64_t offset = ip->i_start_block * BLOCK_SIZE;
        ssize_t bytes = block_read(ip->i_bdev, offset, buf, to_read);
        if (bytes < 0) {
            uart_puts("[VFS] ERROR: block_read failed\n");
            return -1;
        }
        return (int)bytes;
    }
    
    /* Fallback: read from legacy page cache */
    struct page_cache_entry* entry = ip->cache_list;
    if (!entry || !entry->physical_addr) {
        return 0;
    }
    
    memcpy(buf, entry->physical_addr, to_read);
    return (int)to_read;
}

/* ========== Directory & Path Operations ========== */

struct vnode* vfs_lookup(struct vnode *dir, const char *name) {
    if (!dir || !name || dir->v_type != VDIR) return NULL;

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
            return de->vnode;
        }
        de = de->next;
    }
    return NULL;
}

struct vnode* vfs_resolve_path(const char *path) {
    if (!path || *path == '\0') return NULL;

    struct vnode *cur = root_vnode;
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

    /* Check for duplicate */
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

    /* Prepend to list */
    de->next = dir->v_children;
    dir->v_children = de;
    dir->v_nchildren++;

    /* Set parent */
    child->v_parent = dir;

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
    
    /* If we have a block device, write through buffer cache */
    if (ip->i_bdev) {
        /* Allocate blocks if not yet allocated */
        if (ip->i_blocks == 0) {
            /* Calculate blocks needed (round up) */
            uint64_t blocks_needed = (nbyte + BLOCK_SIZE - 1) / BLOCK_SIZE;
            if (blocks_needed == 0) blocks_needed = 1;
            
            if (vfs_alloc_blocks(ip, blocks_needed) != 0) {
                uart_puts("[VFS] ERROR: Failed to allocate blocks\n");
                return -1;
            }
        }
        
        /* Write through buffer cache */
        uint64_t offset = ip->i_start_block * BLOCK_SIZE;
        ssize_t bytes = block_write(ip->i_bdev, offset, buf, nbyte);
        if (bytes < 0) {
            uart_puts("[VFS] ERROR: block_write failed\n");
            return -1;
        }
        
        ip->i_size = (uint32_t)nbyte;
        return (int)bytes;
    }
    
    /* Fallback: use legacy page cache (no block device) */
    if (!ip->cache_list) {
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
        ip->cache_list = entry;
    }
    
    struct page_cache_entry* entry = ip->cache_list;
    size_t to_write = nbyte;
    if (to_write > 4096) to_write = 4096;  /* Limit to one page */
    
    memcpy(entry->physical_addr, buf, to_write);
    entry->is_dirty = 1;
    ip->i_size = (uint32_t)to_write;
    
    return (int)to_write;
}
