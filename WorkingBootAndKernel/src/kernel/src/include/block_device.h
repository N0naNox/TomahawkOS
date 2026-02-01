/**
 * @file block_device.h
 * @brief Block Device Abstraction Layer
 * 
 * Provides a generic interface for block devices with buffer cache.
 * Designed to work with RAM-based filesystem now, easily switchable
 * to physical disk drivers later.
 */

#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdint.h>
#include <stddef.h>

/* Define ssize_t if not available (signed size_t for error returns) */
typedef long ssize_t;

/* Block size - standard 512 bytes, but we use 4K for page alignment */
#define BLOCK_SIZE 4096
#define BLOCKS_PER_PAGE 1

/* Buffer cache configuration */
#define BUFFER_CACHE_SIZE 64  /* Number of cached blocks */

/* Buffer flags */
#define BUF_VALID   0x01  /* Buffer contains valid data */
#define BUF_DIRTY   0x02  /* Buffer has been modified */
#define BUF_BUSY    0x04  /* Buffer is in use */
#define BUF_PINNED  0x08  /* Buffer cannot be evicted */

/* Forward declarations */
struct block_device;
struct buffer_head;

/**
 * @brief Block device operations
 * 
 * Function pointers for device-specific I/O operations.
 * Implement these for each type of block device (RAM, ATA, NVMe, etc.)
 */
struct block_device_ops {
    /**
     * @brief Read a block from the device
     * @param dev The block device
     * @param block_num Block number to read
     * @param buffer Destination buffer (must be BLOCK_SIZE bytes)
     * @return 0 on success, negative error code on failure
     */
    int (*read_block)(struct block_device *dev, uint64_t block_num, void *buffer);
    
    /**
     * @brief Write a block to the device
     * @param dev The block device
     * @param block_num Block number to write
     * @param buffer Source buffer (must be BLOCK_SIZE bytes)
     * @return 0 on success, negative error code on failure
     */
    int (*write_block)(struct block_device *dev, uint64_t block_num, const void *buffer);
    
    /**
     * @brief Flush device buffers (sync to storage)
     * @param dev The block device
     * @return 0 on success, negative error code on failure
     */
    int (*sync)(struct block_device *dev);
    
    /**
     * @brief Get total number of blocks on device
     * @param dev The block device
     * @return Number of blocks, or 0 on error
     */
    uint64_t (*get_size)(struct block_device *dev);
};

/**
 * @brief Block device structure
 * 
 * Represents a block device (disk, ramdisk, etc.)
 */
struct block_device {
    char name[32];                      /* Device name (e.g., "ram0", "sda") */
    uint32_t block_size;                /* Block size in bytes */
    uint64_t total_blocks;              /* Total number of blocks */
    struct block_device_ops *ops;       /* Device operations */
    void *private_data;                 /* Device-specific data */
    int flags;                          /* Device flags */
    uint32_t ref_count;                 /* Reference count */
};

/**
 * @brief Buffer head - represents a cached block
 * 
 * The buffer cache sits between VFS and block devices,
 * caching recently accessed blocks to minimize I/O.
 */
struct buffer_head {
    struct block_device *dev;           /* Associated device */
    uint64_t block_num;                 /* Block number on device */
    uint8_t *data;                      /* Pointer to cached data */
    uint32_t flags;                     /* Buffer flags (BUF_*) */
    uint32_t ref_count;                 /* Reference count */
    struct buffer_head *hash_next;      /* Hash chain for lookup */
    struct buffer_head *lru_prev;       /* LRU list previous */
    struct buffer_head *lru_next;       /* LRU list next */
};

/* ========== Block Device Management ========== */

/**
 * @brief Initialize the block device subsystem
 */
void block_device_init(void);

/**
 * @brief Register a new block device
 * @param dev The device to register
 * @return 0 on success, negative error code on failure
 */
int block_device_register(struct block_device *dev);

/**
 * @brief Unregister a block device
 * @param dev The device to unregister
 * @return 0 on success, negative error code on failure
 */
int block_device_unregister(struct block_device *dev);

/**
 * @brief Find a block device by name
 * @param name Device name to search for
 * @return Pointer to device, or NULL if not found
 */
struct block_device *block_device_get(const char *name);

/**
 * @brief Release a reference to a block device
 * @param dev The device to release
 */
void block_device_put(struct block_device *dev);

/* ========== Buffer Cache API ========== */

/**
 * @brief Initialize the buffer cache
 */
void buffer_cache_init(void);

/**
 * @brief Get a buffer for a specific block
 * 
 * Returns a buffer for the specified block, reading from disk if necessary.
 * The buffer is locked on return.
 * 
 * @param dev The block device
 * @param block_num Block number to get
 * @return Buffer head, or NULL on error
 */
struct buffer_head *buffer_get(struct block_device *dev, uint64_t block_num);

/**
 * @brief Get a buffer without reading from disk
 * 
 * Used when you know you're going to overwrite the entire block.
 * 
 * @param dev The block device
 * @param block_num Block number to get
 * @return Buffer head, or NULL on error
 */
struct buffer_head *buffer_get_new(struct block_device *dev, uint64_t block_num);

/**
 * @brief Release a buffer
 * 
 * Decrements reference count and allows buffer to be reused.
 * 
 * @param bh The buffer to release
 */
void buffer_release(struct buffer_head *bh);

/**
 * @brief Mark a buffer as dirty
 * 
 * Indicates the buffer has been modified and needs to be written back.
 * 
 * @param bh The buffer to mark dirty
 */
void buffer_mark_dirty(struct buffer_head *bh);

/**
 * @brief Write a dirty buffer back to disk
 * @param bh The buffer to sync
 * @return 0 on success, negative error code on failure
 */
int buffer_sync(struct buffer_head *bh);

/**
 * @brief Sync all dirty buffers for a device
 * @param dev The device (NULL for all devices)
 * @return 0 on success, negative error code on failure
 */
int buffer_sync_all(struct block_device *dev);

/**
 * @brief Invalidate all buffers for a device
 * @param dev The device
 */
void buffer_invalidate(struct block_device *dev);

/* ========== High-Level Block I/O ========== */

/**
 * @brief Read data from a block device
 * 
 * High-level read function that handles block boundaries and caching.
 * 
 * @param dev The block device
 * @param offset Byte offset to start reading
 * @param buffer Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes read, or negative error code
 */
ssize_t block_read(struct block_device *dev, uint64_t offset, void *buffer, size_t size);

/**
 * @brief Write data to a block device
 * 
 * High-level write function that handles block boundaries and caching.
 * 
 * @param dev The block device
 * @param offset Byte offset to start writing
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
ssize_t block_write(struct block_device *dev, uint64_t offset, const void *buffer, size_t size);

/* ========== RAM Block Device ========== */

/**
 * @brief Create a RAM-based block device
 * 
 * Creates a block device backed by RAM. Useful for testing
 * and as a template for real disk drivers.
 * 
 * @param name Device name
 * @param num_blocks Number of blocks (each BLOCK_SIZE bytes)
 * @return Pointer to device, or NULL on error
 */
struct block_device *ramblock_create(const char *name, uint64_t num_blocks);

/**
 * @brief Destroy a RAM block device
 * @param dev The device to destroy
 */
void ramblock_destroy(struct block_device *dev);

/* ========== Statistics ========== */

/**
 * @brief Buffer cache statistics
 */
struct buffer_cache_stats {
    uint64_t hits;          /* Cache hits */
    uint64_t misses;        /* Cache misses */
    uint64_t reads;         /* Total block reads */
    uint64_t writes;        /* Total block writes */
    uint64_t syncs;         /* Sync operations */
    uint32_t cached_blocks; /* Currently cached blocks */
    uint32_t dirty_blocks;  /* Dirty blocks in cache */
};

/**
 * @brief Get buffer cache statistics
 * @param stats Pointer to stats structure to fill
 */
void buffer_cache_get_stats(struct buffer_cache_stats *stats);

#endif /* BLOCK_DEVICE_H */
