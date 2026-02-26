#pragma once
#include <stdint.h>
#include "spinlock.h"

/* Forward declaration */
struct block_device;

struct page_cache_entry {
    uint64_t offset;          // File offset (must be multiple of PAGE_SIZE)
    void* physical_addr;      // Physical address in RAM (from frame_alloc)
    int is_dirty;             // Has data been modified?
    struct page_cache_entry* next;
};


struct inode {
    uint32_t i_no;          /* Inode number */
    uint32_t i_size;        /* File size in bytes */
    uint16_t i_mode;        /* Type (file/dir) and permissions */
    uint32_t i_refcount;    /* Reference count */
    void* i_private;
    struct page_cache_entry* cache_list;  /* Page cache list (legacy) */

    /* Block device backing storage */
    struct block_device *i_bdev;    /* Block device for storage */
    uint64_t i_start_block;         /* Starting block number on device */
    uint64_t i_blocks;              /* Number of blocks allocated */

    /**
     * Per-inode spinlock.
     * Protects: i_refcount, i_size, i_mode, i_blocks, i_start_block,
     * and cache_list.  Use spin_lock_irqsave / spin_unlock_irqrestore.
     */
    spinlock_t i_lock;
};