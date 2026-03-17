/*
 * frame_alloc.h - Physical page frame allocator interface
 */

#ifndef FRAME_ALLOC_H
#define FRAME_ALLOC_H

#include <stdint.h>
#include <boot.h>

/*
 * Initialize the frame allocator from the boot info's memory map.
 * Must be called early in kernel_main before any paging or memory allocation.
 */
void frame_alloc_init(Boot_Info* boot_info);

/*
 * Allocate one free 4 KiB physical frame.
 * Returns physical address (page-aligned) or 0 if out of memory.
 */
uintptr_t pfa_alloc_frame(void);

/*
 * Free one physical frame (mark it as available for reallocation).
 */
void pfa_free_frame(uintptr_t paddr);

#endif /* FRAME_ALLOC_H */
