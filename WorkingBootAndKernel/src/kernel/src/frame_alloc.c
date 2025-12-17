/*
 * frame_alloc.c - Physical page frame allocator using UEFI memory map
 * 
 * Parses the memory map from Boot_Info and maintains a bitmap to track
 * which physical pages (4 KiB frames) are free or allocated.
 */

#include "include/frame_alloc.h"
#include "include/paging.h"
#include <uart.h>
#include <string.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Memory map info from boot */
static Memory_Map_Descriptor* g_memmap = NULL;
static uint64_t g_memmap_size = 0;
static uint64_t g_memmap_desc_size = 0;

/* Bitmap: each bit represents one 4 KiB frame. 1 = free, 0 = allocated */
#define BITMAP_PAGES 256 /* 256 * 4 KiB = 1 MiB bitmap => ~32 GiB coverage */
#define BITMAP_SIZE (BITMAP_PAGES * PAGE_SIZE)
static uint8_t g_bitmap[BITMAP_SIZE];
static uint64_t g_bitmap_bits = 0;

/* Track the highest physical address we've seen */
static uintptr_t g_max_phys = 0;

/* Basic accounting */
static uint64_t g_total_pages = 0;  /* pages represented in bitmap */
static uint64_t g_free_pages = 0;   /* free pages available */
static uint64_t g_reserved_pages = 0; /* pages skipped (low or out of range) */

/*
 * Initialize frame allocator from boot info's memory map.
 * Mark all usable RAM regions as free (1), everything else as allocated (0).
 */
void frame_alloc_init(Boot_Info* boot_info)
{
    if (!boot_info || !boot_info->memory_map) {
        uart_puts("frame_alloc: no boot_info or memory_map\n");
        return;
    }

    g_memmap = boot_info->memory_map;
    g_memmap_size = boot_info->memory_map_size;
    g_memmap_desc_size = boot_info->memory_map_descriptor_size;
    g_bitmap_bits = BITMAP_SIZE * 8;
    g_total_pages = g_bitmap_bits; /* what the bitmap can represent */
    g_free_pages = 0;
    g_reserved_pages = 0;

    /* Initialize bitmap: all frames allocated by default */
    memset(g_bitmap, 0, BITMAP_SIZE);

    /* Parse memory map and mark usable regions as free */
    uint64_t usable_pages = 0;
    for (uint64_t i = 0; i < g_memmap_size; i += g_memmap_desc_size) {
        Memory_Map_Descriptor* desc = 
            (Memory_Map_Descriptor*)((uint8_t*)g_memmap + i);

        /* Types considered usable after ExitBootServices: Conventional (7), BootServicesCode (5), BootServicesData (6) */
        if (desc->type == 7 || desc->type == 6 || desc->type == 5) {
            uintptr_t start_frame = desc->physical_start / PAGE_SIZE;
            uint64_t n_frames = desc->count;
            uintptr_t end_frame = start_frame + n_frames;

            if (end_frame * PAGE_SIZE > g_max_phys) g_max_phys = end_frame * PAGE_SIZE;

            /* Mark frames as free if they fit in our bitmap */
            for (uint64_t j = start_frame; j < end_frame && j < g_bitmap_bits; j++) {
                /* Never hand out frame 0 or very low memory (<1 MiB) to avoid null/BIOS areas */
                if (j < (0x100000ULL / PAGE_SIZE)) {
                    g_reserved_pages++;
                    continue;
                }
                uint64_t byte_off = j / 8;
                uint8_t bit_off = j % 8;
                g_bitmap[byte_off] |= (1 << bit_off);
                usable_pages++;
                g_free_pages++;
            }
        }
    }

    uintptr_t coverage_bytes = g_bitmap_bits * PAGE_SIZE;
    if (g_max_phys > coverage_bytes) {
        uart_puts("frame_alloc: WARNING coverage limited to 0x");
        uart_puthex(coverage_bytes);
        uart_puts(" bytes, max phys seen 0x");
        uart_puthex(g_max_phys);
        uart_puts("\n");
    }

    uart_puts("frame_alloc: initialized with ~");
    uart_putu(usable_pages);
    uart_puts(" usable pages (max phys: 0x");
    uart_puthex(g_max_phys);
    uart_puts(", bitmap covers: 0x");
    uart_puthex(coverage_bytes);
    uart_puts(")\n");
}

/*
 * Allocate one free 4 KiB frame.
 * Returns physical address of the frame, or 0 if none available.
 */
uintptr_t pfa_alloc_frame(void)
{
    /* Simple linear search through bitmap */
    for (uint64_t i = 0; i < g_bitmap_bits; i++) {
        uint64_t byte_off = i / 8;
        uint8_t bit_off = i % 8;

        if (byte_off >= BITMAP_SIZE) {
            uart_puts("frame_alloc: bitmap exhausted\n");
            return 0;
        }

        /* Check if this frame is free (bit = 1) */
        if (g_bitmap[byte_off] & (1 << bit_off)) {
            /* Mark as allocated (bit = 0) */
            g_bitmap[byte_off] &= ~(1 << bit_off);
            if (g_free_pages > 0) g_free_pages--;
            uintptr_t phys = i * PAGE_SIZE;
            return phys;
        }
    }

    uart_puts("frame_alloc: out of frames\n");
    return 0;
}

/*
 * Free one physical frame (mark it as free in bitmap).
 */
void pfa_free_frame(uintptr_t paddr)
{
    if (paddr == 0) return;

    uint64_t frame_idx = paddr / PAGE_SIZE;
    if (frame_idx >= g_bitmap_bits) {
        uart_puts("frame_alloc: free out of range\n");
        return;
    }

    uint64_t byte_off = frame_idx / 8;
    uint8_t bit_off = frame_idx % 8;

    /* Mark as free (bit = 1) */
    if ((g_bitmap[byte_off] & (1 << bit_off)) == 0) {
        g_bitmap[byte_off] |= (1 << bit_off);
        g_free_pages++;
    }
}
