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
