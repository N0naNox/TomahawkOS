/*
 * refcount.h - Reference counting for physical page frames
 * 
 * Tracks how many page tables reference each physical frame.
 * Essential for Copy-On-Write (COW) implementation.
 */

#ifndef REFCOUNT_H
#define REFCOUNT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Initialize the reference counting system.
 * Must be called after frame_alloc_init().
 * 
 * @param max_frames: Maximum number of physical frames to track
 */
void refcount_init(size_t max_frames);

/*
 * Increment the reference count for a physical frame.
 * 
 * @param paddr: Physical address of the frame (will be page-aligned)
 * @return: New reference count, or 0 on error
 */
uint32_t refcount_inc(uintptr_t paddr);

/*
 * Decrement the reference count for a physical frame.
 * If the count reaches zero, the frame can be freed.
 * 
 * @param paddr: Physical address of the frame (will be page-aligned)
 * @return: New reference count, or 0 if frame should be freed
 */
uint32_t refcount_dec(uintptr_t paddr);

/*
 * Get the current reference count for a physical frame.
 * 
 * @param paddr: Physical address of the frame (will be page-aligned)
 * @return: Current reference count (0 = not allocated or error)
 */
uint32_t refcount_get(uintptr_t paddr);

/*
 * Set the reference count for a physical frame.
 * Mainly used during initialization or special cases.
 * 
 * @param paddr: Physical address of the frame
 * @param count: New reference count value
 */
void refcount_set(uintptr_t paddr, uint32_t count);

/*
 * Check if a frame is shared (refcount > 1)
 * 
 * @param paddr: Physical address of the frame
 * @return: 1 if shared, 0 if not shared or error
 */
int refcount_is_shared(uintptr_t paddr);

#endif /* REFCOUNT_H */
