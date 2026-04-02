/**
 * @file block_device.c
 * @brief Block Device Abstraction Layer Implementation
 * 
 * Implements:
 * - Block device registration and management
 * - Buffer cache with LRU eviction
 * - RAM-based block device for testing
 */

#include "include/block_device.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include "include/spinlock.h"
#include <uart.h>

/* ========== Internal Structures ========== */

/* Hash table size for buffer lookup - must be power of 2 */
#define BUFFER_HASH_SIZE 32
#define BUFFER_HASH_MASK (BUFFER_HASH_SIZE - 1)

/* Maximum registered devices */
#define MAX_BLOCK_DEVICES 16

/* RAM block device private data */
struct ramblock_data {
    uint8_t **blocks;       /* Array of block pointers */
    uint64_t num_blocks;    /* Number of blocks */
};

/* ========== Global State ========== */

/* Registered block devices */
static struct block_device *registered_devices[MAX_BLOCK_DEVICES];
static int num_devices = 0;

/* Buffer cache */
static struct buffer_head *buffer_hash[BUFFER_HASH_SIZE];
static struct buffer_head *lru_head = NULL;  /* Most recently used */
static struct buffer_head *lru_tail = NULL;  /* Least recently used */
static struct buffer_head *free_buffers = NULL;
static struct buffer_head buffer_pool[BUFFER_CACHE_SIZE];
static uint8_t *buffer_data_pool = NULL;

/* Statistics */
static struct buffer_cache_stats cache_stats = {0};

/* Initialization flag */
static int block_subsystem_initialized = 0;

/*
 * Buffer-cache lock — protects buffer_hash[], lru_head/tail, free_buffers,
 * buffer_pool[], and cache_stats.  Always acquired with IRQ-save so that
 * interrupt-driven I/O completion paths cannot deadlock.
 * Internal (static) helpers are called with the lock already held.
 */
static spinlock_t s_cache_lock = SPINLOCK_INIT;

/*
 * Device-registry lock — protects registered_devices[] and num_devices.
 */
static spinlock_t s_dev_lock = SPINLOCK_INIT;

/* ========== Helper Functions ========== */

/* Forward declaration — buffer_sync_internal is defined after buffer_cache_init
   but is called inside buffer_alloc (also a helper).  Caller must hold s_cache_lock. */
static int buffer_sync_internal(struct buffer_head *bh);

/* Hash function for buffer lookup */
static uint32_t buffer_hash_func(struct block_device *dev, uint64_t block_num) {
    /* Simple hash combining device pointer and block number */
    uint64_t h = (uint64_t)(uintptr_t)dev ^ block_num;
    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    return (uint32_t)(h & BUFFER_HASH_MASK);
}

/* Remove buffer from LRU list */
static void lru_remove(struct buffer_head *bh) {
    if (bh->lru_prev) {
        bh->lru_prev->lru_next = bh->lru_next;
    } else {
        lru_head = bh->lru_next;
    }
    if (bh->lru_next) {
        bh->lru_next->lru_prev = bh->lru_prev;
    } else {
        lru_tail = bh->lru_prev;
    }
    bh->lru_prev = NULL;
    bh->lru_next = NULL;
}

/* Add buffer to front of LRU list (most recently used) */
static void lru_add_front(struct buffer_head *bh) {
    bh->lru_prev = NULL;
    bh->lru_next = lru_head;
    if (lru_head) {
        lru_head->lru_prev = bh;
    } else {
        lru_tail = bh;
    }
    lru_head = bh;
}

/* Move buffer to front of LRU (on access) */
static void lru_touch(struct buffer_head *bh) {
    if (bh == lru_head) return;  /* Already at front */
    lru_remove(bh);
    lru_add_front(bh);
}

/* Find buffer in hash table */
static struct buffer_head *buffer_find(struct block_device *dev, uint64_t block_num) {
    uint32_t hash = buffer_hash_func(dev, block_num);
    struct buffer_head *bh = buffer_hash[hash];
    
    while (bh) {
        if (bh->dev == dev && bh->block_num == block_num) {
            return bh;
        }
        bh = bh->hash_next;
    }
    return NULL;
}

/* Add buffer to hash table */
static void buffer_hash_add(struct buffer_head *bh) {
    uint32_t hash = buffer_hash_func(bh->dev, bh->block_num);
    bh->hash_next = buffer_hash[hash];
    buffer_hash[hash] = bh;
}

/* Remove buffer from hash table */
static void buffer_hash_remove(struct buffer_head *bh) {
    uint32_t hash = buffer_hash_func(bh->dev, bh->block_num);
    struct buffer_head **pp = &buffer_hash[hash];
    
    while (*pp) {
        if (*pp == bh) {
            *pp = bh->hash_next;
            bh->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* Get a free buffer (allocate or evict) */
static struct buffer_head *buffer_alloc(void) {
    struct buffer_head *bh;
    
    /* Try free list first */
    if (free_buffers) {
        bh = free_buffers;
        free_buffers = bh->hash_next;
        bh->hash_next = NULL;
        return bh;
    }
    
    /* Find LRU buffer to evict */
    bh = lru_tail;
    while (bh) {
        /* Skip busy, pinned, or referenced buffers */
        if (!(bh->flags & (BUF_BUSY | BUF_PINNED)) && bh->ref_count == 0) {
            /* Write back if dirty */
            if (bh->flags & BUF_DIRTY) {
                buffer_sync_internal(bh);  /* called under s_cache_lock */
            }
            
            /* Remove from hash and LRU */
            buffer_hash_remove(bh);
            lru_remove(bh);
            
            /* Clear state */
            bh->dev = NULL;
            bh->block_num = 0;
            bh->flags = 0;
            
            cache_stats.cached_blocks--;
            return bh;
        }
        bh = bh->lru_prev;
    }
    
    /* No buffer available */
    uart_puts("[BLOCK] ERROR: No buffer available for allocation\n");
    return NULL;
}

/* ========== Buffer Cache Implementation ========== */

void buffer_cache_init(void) {
    /* Initialize buffer pool - allocate one page per buffer */
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        uint64_t frame = pfa_alloc_frame();
        if (!frame) {
            uart_puts("[BLOCK] ERROR: Failed to allocate buffer ");
            uart_putu(i);
            uart_puts("\n");
            return;
        }
        
        buffer_pool[i].dev = NULL;
        buffer_pool[i].block_num = 0;
        buffer_pool[i].data = (uint8_t *)frame;  /* Each buffer gets its own page */
        buffer_pool[i].flags = 0;
        buffer_pool[i].ref_count = 0;
        buffer_pool[i].hash_next = (i < BUFFER_CACHE_SIZE - 1) ? &buffer_pool[i + 1] : NULL;
        buffer_pool[i].lru_prev = NULL;
        buffer_pool[i].lru_next = NULL;
    }
    
    free_buffers = &buffer_pool[0];
    buffer_data_pool = buffer_pool[0].data;  /* Just for reference */
    
    /* Clear hash table */
    for (int i = 0; i < BUFFER_HASH_SIZE; i++) {
        buffer_hash[i] = NULL;
    }
    
    /* Clear stats */
    memset(&cache_stats, 0, sizeof(cache_stats));
    
    uart_puts("[BLOCK] Buffer cache initialized (");
    uart_putu(BUFFER_CACHE_SIZE);
    uart_puts(" buffers, ");
    uart_putu(BLOCK_SIZE);
    uart_puts(" bytes each)\n");
}

struct buffer_head *buffer_get(struct block_device *dev, uint64_t block_num) {
    if (!dev || !dev->ops || !dev->ops->read_block) {
        return NULL;
    }

    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);

    /* Check if already cached */
    struct buffer_head *bh = buffer_find(dev, block_num);
    if (bh) {
        bh->ref_count++;
        bh->flags |= BUF_BUSY;
        lru_touch(bh);
        cache_stats.hits++;
        spin_unlock_irqrestore(&s_cache_lock, &flags);
        return bh;
    }

    cache_stats.misses++;

    /* Allocate new buffer */
    bh = buffer_alloc();
    if (!bh) {
        spin_unlock_irqrestore(&s_cache_lock, &flags);
        return NULL;
    }

    /* Setup buffer */
    bh->dev = dev;
    bh->block_num = block_num;
    bh->ref_count = 1;
    bh->flags = BUF_BUSY;

    /* Read block from device (RAM device — safe to call under lock) */
    if (dev->ops->read_block(dev, block_num, bh->data) != 0) {
        uart_puts("[BLOCK] ERROR: Failed to read block ");
        uart_putu(block_num);
        uart_puts("\n");
        /* Return buffer to free list */
        bh->hash_next = free_buffers;
        free_buffers = bh;
        spin_unlock_irqrestore(&s_cache_lock, &flags);
        return NULL;
    }

    bh->flags |= BUF_VALID;
    cache_stats.reads++;
    cache_stats.cached_blocks++;

    /* Add to hash table and LRU */
    buffer_hash_add(bh);
    lru_add_front(bh);

    spin_unlock_irqrestore(&s_cache_lock, &flags);
    return bh;
}

struct buffer_head *buffer_get_new(struct block_device *dev, uint64_t block_num) {
    if (!dev) {
        return NULL;
    }

    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);

    /* Check if already cached */
    struct buffer_head *bh = buffer_find(dev, block_num);
    if (bh) {
        bh->ref_count++;
        bh->flags |= BUF_BUSY;
        lru_touch(bh);
        /* Mark as valid since we're going to overwrite */
        bh->flags |= BUF_VALID;
        spin_unlock_irqrestore(&s_cache_lock, &flags);
        return bh;
    }

    /* Allocate new buffer without reading */
    bh = buffer_alloc();
    if (!bh) {
        spin_unlock_irqrestore(&s_cache_lock, &flags);
        return NULL;
    }

    /* Setup buffer */
    bh->dev = dev;
    bh->block_num = block_num;
    bh->ref_count = 1;
    bh->flags = BUF_BUSY | BUF_VALID;

    cache_stats.cached_blocks++;

    /* Add to hash table and LRU */
    buffer_hash_add(bh);
    lru_add_front(bh);

    spin_unlock_irqrestore(&s_cache_lock, &flags);
    return bh;
}

void buffer_release(struct buffer_head *bh) {
    if (!bh) return;
    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);
    if (bh->ref_count > 0) {
        bh->ref_count--;
    }
    bh->flags &= ~BUF_BUSY;
    spin_unlock_irqrestore(&s_cache_lock, &flags);
}

void buffer_mark_dirty(struct buffer_head *bh) {
    if (!bh) return;
    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);
    if (!(bh->flags & BUF_DIRTY)) {
        bh->flags |= BUF_DIRTY;
        cache_stats.dirty_blocks++;
    }
    spin_unlock_irqrestore(&s_cache_lock, &flags);
}

int buffer_sync(struct buffer_head *bh) {
    if (!bh || !bh->dev || !(bh->flags & BUF_DIRTY)) {
        return 0;
    }
    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);
    int ret = buffer_sync_internal(bh);
    spin_unlock_irqrestore(&s_cache_lock, &flags);
    return ret;
}

/* Internal sync — caller must hold s_cache_lock */
static int buffer_sync_internal(struct buffer_head *bh) {
    if (!bh || !bh->dev || !(bh->flags & BUF_DIRTY)) {
        return 0;
    }
    
    if (!bh->dev->ops || !bh->dev->ops->write_block) {
        return -1;
    }
    
    /* Write block to device */
    int result = bh->dev->ops->write_block(bh->dev, bh->block_num, bh->data);
    if (result == 0) {
        bh->flags &= ~BUF_DIRTY;
        cache_stats.writes++;
        cache_stats.dirty_blocks--;
        cache_stats.syncs++;
    }
    
    return result;
}

int buffer_sync_all(struct block_device *dev) {
    int errors = 0;

    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);

    /* Walk through all cached buffers */
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        struct buffer_head *bh = &buffer_pool[i];
        if ((bh->flags & BUF_DIRTY) && (!dev || bh->dev == dev)) {
            if (buffer_sync_internal(bh) != 0) {
                errors++;
            }
        }
    }

    spin_unlock_irqrestore(&s_cache_lock, &flags);

    /* Call device sync if available (outside the cache lock) */
    if (dev && dev->ops && dev->ops->sync) {
        dev->ops->sync(dev);
    }

    return errors > 0 ? -1 : 0;
}

void buffer_invalidate(struct block_device *dev) {
    uint64_t flags;
    spin_lock_irqsave(&s_cache_lock, &flags);

    for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
        struct buffer_head *bh = &buffer_pool[i];
        if (bh->dev == dev) {
            /* Sync dirty buffers first (internal, under lock) */
            if (bh->flags & BUF_DIRTY) {
                buffer_sync_internal(bh);
            }

            /* Remove from hash and LRU */
            buffer_hash_remove(bh);
            if (bh->lru_prev || bh->lru_next || bh == lru_head) {
                lru_remove(bh);
            }

            /* Return to free list */
            bh->dev = NULL;
            bh->block_num = 0;
            bh->flags = 0;
            bh->ref_count = 0;
            bh->hash_next = free_buffers;
            free_buffers = bh;

            cache_stats.cached_blocks--;
        }
    }

    spin_unlock_irqrestore(&s_cache_lock, &flags);
}

void buffer_cache_get_stats(struct buffer_cache_stats *stats) {
    if (stats) {
        uint64_t flags;
        spin_lock_irqsave(&s_cache_lock, &flags);
        *stats = cache_stats;
        spin_unlock_irqrestore(&s_cache_lock, &flags);
    }
}

/* ========== High-Level Block I/O ========== */

ssize_t block_read(struct block_device *dev, uint64_t offset, void *buffer, size_t size) {
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    
    uint64_t block_num = offset / BLOCK_SIZE;
    uint64_t block_offset = offset % BLOCK_SIZE;
    size_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buffer;
    
    while (bytes_read < size) {
        /* Check bounds */
        if (block_num >= dev->total_blocks) {
            break;
        }
        
        /* Get block from cache */
        struct buffer_head *bh = buffer_get(dev, block_num);
        if (!bh) {
            return bytes_read > 0 ? (ssize_t)bytes_read : -1;
        }
        
        /* Copy data */
        size_t to_copy = BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }
        
        memcpy(dst + bytes_read, bh->data + block_offset, to_copy);
        bytes_read += to_copy;
        
        buffer_release(bh);
        
        /* Next block */
        block_num++;
        block_offset = 0;
    }
    
    return (ssize_t)bytes_read;
}

ssize_t block_write(struct block_device *dev, uint64_t offset, const void *buffer, size_t size) {
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    
    uint64_t block_num = offset / BLOCK_SIZE;
    uint64_t block_offset = offset % BLOCK_SIZE;
    size_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)buffer;
    
    while (bytes_written < size) {
        /* Check bounds */
        if (block_num >= dev->total_blocks) {
            break;
        }
        
        struct buffer_head *bh;
        
        /* If writing full block, no need to read first */
        if (block_offset == 0 && (size - bytes_written) >= BLOCK_SIZE) {
            bh = buffer_get_new(dev, block_num);
        } else {
            /* Partial block write - need to read first */
            bh = buffer_get(dev, block_num);
        }
        
        if (!bh) {
            return bytes_written > 0 ? (ssize_t)bytes_written : -1;
        }
        
        /* Copy data */
        size_t to_copy = BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_written) {
            to_copy = size - bytes_written;
        }
        
        memcpy(bh->data + block_offset, src + bytes_written, to_copy);
        buffer_mark_dirty(bh);
        bytes_written += to_copy;
        
        buffer_release(bh);
        
        /* Next block */
        block_num++;
        block_offset = 0;
    }
    
    return (ssize_t)bytes_written;
}

/* ========== Block Device Management ========== */

void block_device_init(void) {
    if (block_subsystem_initialized) {
        return;
    }
    
    /* Clear device list */
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        registered_devices[i] = NULL;
    }
    num_devices = 0;
    
    /* Initialize buffer cache */
    buffer_cache_init();
    
    block_subsystem_initialized = 1;
    uart_puts("[BLOCK] Block device subsystem initialized\n");
}

int block_device_register(struct block_device *dev) {
    if (!dev || !dev->ops) {
        return -1;
    }

    uint64_t flags;
    spin_lock_irqsave(&s_dev_lock, &flags);

    if (num_devices >= MAX_BLOCK_DEVICES) {
        spin_unlock_irqrestore(&s_dev_lock, &flags);
        uart_puts("[BLOCK] ERROR: Too many devices\n");
        return -1;
    }

    /* Check for duplicate name */
    for (int i = 0; i < num_devices; i++) {
        if (registered_devices[i] && strcmp(registered_devices[i]->name, dev->name) == 0) {
            spin_unlock_irqrestore(&s_dev_lock, &flags);
            uart_puts("[BLOCK] ERROR: Device name already exists\n");
            return -1;
        }
    }

    registered_devices[num_devices++] = dev;
    dev->ref_count = 1;

    spin_unlock_irqrestore(&s_dev_lock, &flags);

    uart_puts("[BLOCK] Registered device: ");
    uart_puts(dev->name);
    uart_puts(" (");
    uart_putu(dev->total_blocks);
    uart_puts(" blocks)\n");

    return 0;
}

int block_device_unregister(struct block_device *dev) {
    if (!dev) {
        return -1;
    }

    uint64_t flags;
    spin_lock_irqsave(&s_dev_lock, &flags);

    /* Find and remove device from registry */
    for (int i = 0; i < num_devices; i++) {
        if (registered_devices[i] == dev) {
            /* Remove from list while holding dev lock */
            for (int j = i; j < num_devices - 1; j++) {
                registered_devices[j] = registered_devices[j + 1];
            }
            registered_devices[--num_devices] = NULL;

            spin_unlock_irqrestore(&s_dev_lock, &flags);

            /* Invalidate all buffers for this device outside the dev lock
               (buffer_invalidate acquires s_cache_lock internally) */
            buffer_invalidate(dev);

            uart_puts("[BLOCK] Unregistered device: ");
            uart_puts(dev->name);
            uart_puts("\n");

            return 0;
        }
    }

    spin_unlock_irqrestore(&s_dev_lock, &flags);
    return -1;
}

struct block_device *block_device_get(const char *name) {
    uint64_t flags;
    spin_lock_irqsave(&s_dev_lock, &flags);
    for (int i = 0; i < num_devices; i++) {
        if (registered_devices[i] && strcmp(registered_devices[i]->name, name) == 0) {
            registered_devices[i]->ref_count++;
            struct block_device *dev = registered_devices[i];
            spin_unlock_irqrestore(&s_dev_lock, &flags);
            return dev;
        }
    }
    spin_unlock_irqrestore(&s_dev_lock, &flags);
    return NULL;
}

void block_device_put(struct block_device *dev) {
    if (!dev) return;
    uint64_t flags;
    spin_lock_irqsave(&s_dev_lock, &flags);
    if (dev->ref_count > 0) {
        dev->ref_count--;
    }
    spin_unlock_irqrestore(&s_dev_lock, &flags);
}

/* ========== RAM Block Device Implementation ========== */

static int ramblock_read(struct block_device *dev, uint64_t block_num, void *buffer) {
    struct ramblock_data *data = (struct ramblock_data *)dev->private_data;
    
    if (block_num >= data->num_blocks) {
        return -1;
    }
    
    if (data->blocks[block_num]) {
        memcpy(buffer, data->blocks[block_num], BLOCK_SIZE);
    } else {
        /* Block not allocated - return zeros */
        memset(buffer, 0, BLOCK_SIZE);
    }
    
    return 0;
}

static int ramblock_write(struct block_device *dev, uint64_t block_num, const void *buffer) {
    struct ramblock_data *data = (struct ramblock_data *)dev->private_data;
    
    if (block_num >= data->num_blocks) {
        return -1;
    }
    
    /* Allocate block on first write (sparse allocation) */
    if (!data->blocks[block_num]) {
        data->blocks[block_num] = (uint8_t *)pfa_alloc_frame();
        if (!data->blocks[block_num]) {
            return -1;
        }
    }
    
    memcpy(data->blocks[block_num], buffer, BLOCK_SIZE);
    return 0;
}

static int ramblock_sync(struct block_device *dev) {
    /* RAM device - nothing to sync */
    (void)dev;
    return 0;
}

static uint64_t ramblock_get_size(struct block_device *dev) {
    struct ramblock_data *data = (struct ramblock_data *)dev->private_data;
    return data->num_blocks;
}

static struct block_device_ops ramblock_ops = {
    .read_block = ramblock_read,
    .write_block = ramblock_write,
    .sync = ramblock_sync,
    .get_size = ramblock_get_size
};

struct block_device *ramblock_create(const char *name, uint64_t num_blocks) {
    /* Allocate device structure */
    struct block_device *dev = (struct block_device *)pfa_alloc_frame();
    if (!dev) {
        uart_puts("[RAMBLOCK] ERROR: Failed to allocate device\n");
        return NULL;
    }
    
    /* Allocate private data */
    struct ramblock_data *data = (struct ramblock_data *)pfa_alloc_frame();
    if (!data) {
        pfa_free_frame((uintptr_t)dev);
        uart_puts("[RAMBLOCK] ERROR: Failed to allocate private data\n");
        return NULL;
    }
    
    /* Allocate block pointer array */
    size_t ptr_array_size = num_blocks * sizeof(uint8_t *);
    size_t pages_needed = (ptr_array_size + 4095) / 4096;
    
    data->blocks = (uint8_t **)pfa_alloc_frame();
    if (!data->blocks) {
        pfa_free_frame((uintptr_t)data);
        pfa_free_frame((uintptr_t)dev);
        uart_puts("[RAMBLOCK] ERROR: Failed to allocate block array\n");
        return NULL;
    }
    
    /* Allocate additional pages for large block arrays */
    for (size_t i = 1; i < pages_needed; i++) {
        if (!pfa_alloc_frame()) {
            /* Cleanup on failure */
            pfa_free_frame((uintptr_t)data->blocks);
            pfa_free_frame((uintptr_t)data);
            pfa_free_frame((uintptr_t)dev);
            return NULL;
        }
    }
    
    /* Initialize block pointers to NULL (sparse allocation) */
    for (uint64_t i = 0; i < num_blocks; i++) {
        data->blocks[i] = NULL;
    }
    data->num_blocks = num_blocks;
    
    /* Initialize device */
    memset(dev->name, 0, sizeof(dev->name));
    size_t name_len = strlen(name);
    if (name_len >= sizeof(dev->name)) {
        name_len = sizeof(dev->name) - 1;
    }
    memcpy(dev->name, name, name_len);
    
    dev->block_size = BLOCK_SIZE;
    dev->total_blocks = num_blocks;
    dev->ops = &ramblock_ops;
    dev->private_data = data;
    dev->flags = 0;
    dev->ref_count = 0;
    
    uart_puts("[RAMBLOCK] Created device '");
    uart_puts(dev->name);
    uart_puts("' with ");
    uart_putu(num_blocks);
    uart_puts(" blocks (");
    uart_putu(num_blocks * BLOCK_SIZE / 1024);
    uart_puts(" KB)\n");
    
    return dev;
}

void ramblock_destroy(struct block_device *dev) {
    if (!dev) return;
    
    struct ramblock_data *data = (struct ramblock_data *)dev->private_data;
    if (data) {
        /* Free allocated blocks */
        for (uint64_t i = 0; i < data->num_blocks; i++) {
            if (data->blocks[i]) {
                pfa_free_frame((uintptr_t)data->blocks[i]);
            }
        }
        
        /* Free block pointer array */
        pfa_free_frame((uintptr_t)data->blocks);
        pfa_free_frame((uintptr_t)data);
    }
    
    pfa_free_frame((uintptr_t)dev);
}
