/*
 * paging.c
 * Simple 4-level page-table helpers for x86_64 (4 KiB pages).
 *
 * Assumptions:
 *  - You provide a page-frame allocator:
 *      uintptr_t pfa_alloc_frame(void);
 *      void pfa_free_frame(uintptr_t paddr);
 *  - This file uses a simple phys->virt direct mapping:
 *      virt = (void*)(phys + phys_map_offset)
 *    where phys_map_offset is set by paging_init(phys_map_offset).
 *
 * Usage:
 *  - Call paging_init(phys_map_offset) in early kernel init.
 *  - Create root: pml4 = paging_create_pml4();
 *  - Map pages: paging_map_page(pml4, vaddr, paddr, PTE_PRESENT|PTE_RW);
 *  - Switch: paging_load_cr3(pml4);
 *
 */


 //the assumptions exist because waiting for tom to upload page frame allocator

#include <stdint.h>
#include <stddef.h>
#include <string.h> /* for memset */



/* Include header */
#include "paging.h"

/* phys->virt mapping offset (virt = phys + phys_map_offset) */
static uintptr_t g_phys_map_offset = 0;



/* Forward declarations of allocator provided by kernel (implement these) */
uintptr_t pfa_alloc_frame(void)
{
    /* Placeholder implementation; replace with your frame allocator */
    return 0;
}
void pfa_free_frame(uintptr_t paddr)
{
    /* Placeholder implementation; replace with your frame allocator */
}



/* Convert physical address to kernel virtual pointer using offset set by paging_init */
static inline void* phys_to_virt(uintptr_t phys) {
    if (phys == 0) return NULL;
    return (void*)(phys + g_phys_map_offset);
}

/* paging_init: set phys->virt offset (0 for identity mapping) */
void paging_init(uintptr_t phys_map_offset) {
    g_phys_map_offset = phys_map_offset;
}

/* allocate one physical page and zero it; returns physical address or 0 */
static uintptr_t alloc_zeroed_page(void) {
    uintptr_t p = pfa_alloc_frame();
    if (!p) return 0;
    void* v = phys_to_virt(p);
    if (!v) return 0;
    /* zero page */
    uint8_t *b = (uint8_t*)v;
    for (size_t i = 0; i < PAGE_SIZE; i++) b[i] = 0;
    return p;
}

/* helpers to read/write a table entry given the table's physical page */
static inline uint64_t read_entry(uintptr_t table_phys, uint64_t index) {
    uint64_t *tab = (uint64_t*)phys_to_virt(table_phys);
    return tab[index];
}
static inline void write_entry(uintptr_t table_phys, uint64_t index, uint64_t val) {
    uint64_t *tab = (uint64_t*)phys_to_virt(table_phys);
    tab[index] = val;
}

/* Create PML4: allocate and zero a page */
uintptr_t paging_create_pml4(void) {
    return alloc_zeroed_page();
}

/* Ensure a child table exists at parent[index]; create it if absent.
   Returns physical address of child table (present) or 0 on failure. */
static uintptr_t ensure_table(uintptr_t parent_phys, uint64_t index) {
    uint64_t ent = read_entry(parent_phys, index);
    if (ent & PTE_PRESENT) {
        return (uintptr_t)(ent & PTE_ADDR_MASK);
    }
    uintptr_t child = alloc_zeroed_page();
    if (!child) return 0;
    /* Set parent entry: child phys | present | rw */
    uint64_t newent = (child & PTE_ADDR_MASK) | PTE_PRESENT | PTE_RW;
    write_entry(parent_phys, index, newent);
    return child;
}

/* Map a single 4 KiB page (creates intermediate tables as needed). */
int paging_map_page(uintptr_t pml4_phys, uint64_t vaddr, uintptr_t paddr, uint64_t flags) {
    if (!pml4_phys || !paddr) return -1;

    uint64_t i4 = pml4_index(vaddr);
    uint64_t i3 = pdpt_index(vaddr);
    uint64_t i2 = pd_index(vaddr);
    uint64_t i1 = pt_index(vaddr);

    uintptr_t pdpt = ensure_table(pml4_phys, i4);
    if (!pdpt) return -2;
    uintptr_t pd = ensure_table(pdpt, i3);
    if (!pd) return -3;
    uintptr_t pt = ensure_table(pd, i2);
    if (!pt) return -4;

    uint64_t entry = (paddr & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
    entry |= PTE_PRESENT; /* ensure present */
    write_entry(pt, i1, entry);
    return 0;
}

/* Map a consecutive range of pages */
int paging_map_range(uintptr_t pml4_phys, uint64_t vaddr, uintptr_t paddr, size_t n_pages, uint64_t flags) {
    for (size_t i = 0; i < n_pages; i++) {
        int r = paging_map_page(pml4_phys, vaddr + (i << PAGE_SHIFT), paddr + (i << PAGE_SHIFT), flags);
        if (r) return r;
    }
    return 0;
}

/* Unmap a single page (clear PT entry). Does not free parent tables. */
int paging_unmap_page(uintptr_t pml4_phys, uint64_t vaddr) {
    if (!pml4_phys) return -1;

    uint64_t i4 = pml4_index(vaddr);
    uint64_t i3 = pdpt_index(vaddr);
    uint64_t i2 = pd_index(vaddr);
    uint64_t i1 = pt_index(vaddr);

    uint64_t ent4 = read_entry(pml4_phys, i4);
    if (!(ent4 & PTE_PRESENT)) return -2;
    uintptr_t pdpt = (uintptr_t)(ent4 & PTE_ADDR_MASK);

    uint64_t ent3 = read_entry(pdpt, i3);
    if (!(ent3 & PTE_PRESENT)) return -3;
    uintptr_t pd = (uintptr_t)(ent3 & PTE_ADDR_MASK);

    uint64_t ent2 = read_entry(pd, i2);
    if (!(ent2 & PTE_PRESENT)) return -4;
    uintptr_t pt = (uintptr_t)(ent2 & PTE_ADDR_MASK);

    write_entry(pt, i1, 0); /* clear entry */
    /* Note: we do not INVLPG here; caller should if necessary, or switch CR3. */
    return 0;
}

/* Return mapped physical address for vaddr (handles 1 GiB and 2 MiB PS bits) */
uintptr_t paging_get_phys(uintptr_t pml4_phys, uint64_t vaddr) {
    if (!pml4_phys) return 0;

    uint64_t i4 = pml4_index(vaddr);
    uint64_t i3 = pdpt_index(vaddr);
    uint64_t i2 = pd_index(vaddr);
    uint64_t i1 = pt_index(vaddr);

    uint64_t ent4 = read_entry(pml4_phys, i4);
    if (!(ent4 & PTE_PRESENT)) return 0;
    uintptr_t pdpt = (uintptr_t)(ent4 & PTE_ADDR_MASK);

    uint64_t ent3 = read_entry(pdpt, i3);
    if (!(ent3 & PTE_PRESENT)) return 0;
    if (ent3 & PTE_PS) {
        /* 1 GiB page */
        uintptr_t base = (uintptr_t)(ent3 & PTE_ADDR_MASK);
        uint64_t off = vaddr & ((1ULL << 30) - 1);
        return base + off;
    }

    uintptr_t pd = (uintptr_t)(ent3 & PTE_ADDR_MASK);
    uint64_t ent2 = read_entry(pd, i2);
    if (!(ent2 & PTE_PRESENT)) return 0;
    if (ent2 & PTE_PS) {
        /* 2 MiB page */
        uintptr_t base = (uintptr_t)(ent2 & PTE_ADDR_MASK);
        uint64_t off = vaddr & ((1ULL << 21) - 1);
        return base + off;
    }

    uintptr_t pt = (uintptr_t)(ent2 & PTE_ADDR_MASK);
    uint64_t ent1 = read_entry(pt, i1);
    if (!(ent1 & PTE_PRESENT)) return 0;
    uintptr_t base = (uintptr_t)(ent1 & PTE_ADDR_MASK);
    uint64_t off = vaddr & ((1ULL << 12) - 1);
    return base + off;
}

/* Optional: free a whole PML4 tree (not implemented); placeholder */
void paging_free_pml4(uintptr_t pml4_phys) {
    (void)pml4_phys;
    /* Implement recursively walking and freeing child tables if you want. */
}