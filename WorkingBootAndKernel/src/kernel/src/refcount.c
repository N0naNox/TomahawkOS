/*
 * refcount.c - Reference counting implementation for physical frames
 */

#include "include/refcount.h"
#include "include/paging.h"
#include <uart.h>
#include <string.h>

/* Maximum frames we can track (configurable at init) */
static size_t g_max_frames = 0;

/* Reference count array: one uint32_t per frame */
static uint32_t* g_refcounts = NULL;

/* Static storage for refcounts (supports ~8GB with 4KB pages) */
#define MAX_REFCOUNT_ENTRIES (2 * 1024 * 1024)  /* 2M frames = 8GB */
static uint32_t g_refcount_storage[MAX_REFCOUNT_ENTRIES];

void refcount_init(size_t max_frames)
{
    if (max_frames > MAX_REFCOUNT_ENTRIES) {
        uart_puts("refcount_init: WARNING limiting max_frames from ");
        uart_putu(max_frames);
        uart_puts(" to ");
        uart_putu(MAX_REFCOUNT_ENTRIES);
        uart_puts("\n");
        max_frames = MAX_REFCOUNT_ENTRIES;
    }

    g_max_frames = max_frames;
    g_refcounts = g_refcount_storage;

    /* Initialize all refcounts to 0 */
    memset(g_refcounts, 0, max_frames * sizeof(uint32_t));

    uart_puts("refcount_init: tracking ");
    uart_putu(max_frames);
    uart_puts(" frames (");
    uart_putu((max_frames * PAGE_SIZE) / (1024 * 1024));
    uart_puts(" MB)\n");
}

/* Convert physical address to frame index */
static inline size_t paddr_to_frame(uintptr_t paddr)
{
    return (paddr & ~(PAGE_SIZE - 1)) / PAGE_SIZE;
}

uint32_t refcount_inc(uintptr_t paddr)
{
    size_t frame = paddr_to_frame(paddr);
    
    if (frame >= g_max_frames) {
        uart_puts("refcount_inc: frame ");
        uart_putu(frame);
        uart_puts(" out of range\n");
        return 0;
    }

    if (g_refcounts[frame] == 0xFFFFFFFF) {
        uart_puts("refcount_inc: overflow for frame ");
        uart_putu(frame);
        uart_puts("\n");
        return 0xFFFFFFFF;
    }

    return ++g_refcounts[frame];
}

uint32_t refcount_dec(uintptr_t paddr)
{
    size_t frame = paddr_to_frame(paddr);
    
    if (frame >= g_max_frames) {
        uart_puts("refcount_dec: frame ");
        uart_putu(frame);
        uart_puts(" out of range\n");
        return 0;
    }

    if (g_refcounts[frame] == 0) {
        uart_puts("refcount_dec: underflow for frame ");
        uart_putu(frame);
        uart_puts("\n");
        return 0;
    }

    return --g_refcounts[frame];
}

uint32_t refcount_get(uintptr_t paddr)
{
    size_t frame = paddr_to_frame(paddr);
    
    if (frame >= g_max_frames) {
        return 0;
    }

    return g_refcounts[frame];
}

void refcount_set(uintptr_t paddr, uint32_t count)
{
    size_t frame = paddr_to_frame(paddr);
    
    if (frame >= g_max_frames) {
        uart_puts("refcount_set: frame ");
        uart_putu(frame);
        uart_puts(" out of range\n");
        return;
    }

    g_refcounts[frame] = count;
}

int refcount_is_shared(uintptr_t paddr)
{
    size_t frame = paddr_to_frame(paddr);
    
    if (frame >= g_max_frames) {
        return 0;
    }

    return g_refcounts[frame] > 1 ? 1 : 0;
}
