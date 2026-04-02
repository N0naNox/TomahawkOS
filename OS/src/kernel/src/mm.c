/* ==========================================================
 * File: mm.c
 * Purpose: Implementation of mm_struct, VMA list, simple allocator
 * ==========================================================
 */

#include "mm.h"
#include "paging.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>

/* Simple bump allocator for vm_area_structs and mm_structs */
#define MM_POOL_SIZE_PAGES 4
#define PAGE_SIZE 4096
static uint8_t mm_pool[MM_POOL_SIZE_PAGES * PAGE_SIZE];
static size_t mm_pool_off = 0;

static void* mm_kalloc(size_t size)
{
    /* align to pointer */
    size = (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    if (mm_pool_off + size > sizeof(mm_pool)) return NULL;
    void* p = &mm_pool[mm_pool_off];
    mm_pool_off += size;
    return p;
}

static void mm_kfree(void* p) { (void)p; /* no-op for bump allocator */ }

/* Exported current mm */
mm_struct* current_mm = NULL;

/* Read CR3 */
static inline uintptr_t read_cr3(void)
{
    uintptr_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
    return cr3;
}

/* Load CR3 */
static inline void write_cr3(uintptr_t pml4_phys)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r" (pml4_phys) : "memory");
}

mm_struct* mm_create(void)
{
    mm_struct* mm = (mm_struct*)mm_kalloc(sizeof(mm_struct));
    if (!mm) return NULL;

    /* create an empty PML4 (physical page) */
    uintptr_t pml4 = paging_create_pml4();
    if (!pml4) {
        uart_puts("mm: failed to create pml4\n");
        return NULL;
    }

    mm->pml4_phys = pml4;
    mm->mmap = NULL;
    return mm;
}

void mm_destroy(mm_struct* mm)
{
    if (!mm) return;
    mm_kfree(mm);
}

int mm_add_vma(mm_struct* mm, uintptr_t start, uintptr_t end, uint32_t flags)
{
    if (!mm || start >= end) return -1;

    vm_area_struct* v = (vm_area_struct*)mm_kalloc(sizeof(vm_area_struct));
    if (!v) return -1;

    v->start = start;
    v->end = end;
    v->flags = flags;
    v->next = mm->mmap;
    mm->mmap = v;

    return 0;
}

vm_area_struct* mm_find_vma(mm_struct* mm, uintptr_t addr)
{
    if (!mm) return NULL;
    vm_area_struct* v = mm->mmap;
    while (v) {
        if (addr >= v->start && addr < v->end) return v;
        v = v->next;
    }
    return NULL;
}

void mm_switch(mm_struct* mm)
{
    if (!mm) return;
    write_cr3(mm->pml4_phys);
    current_mm = mm;
}






