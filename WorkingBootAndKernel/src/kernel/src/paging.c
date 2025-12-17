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

#include <stdint.h>
#include <stddef.h>
#include <string.h> /* for memset */
#include <uart.h>

/* Include header */
#include "paging.h"
#include "frame_alloc.h"

/* phys->virt mapping offset (virt = phys + phys_map_offset) */
static uintptr_t g_phys_map_offset = 0;

/* Higher-half base for the kernel mapping (virt alias of the physical kernel). */
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL



/* Forward declarations of allocator provided by kernel (implement these) */
/* Now provided by frame_alloc.c - removed placeholders */




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
    if (!child) {
        uart_puts("paging: alloc_zeroed_page failed in ensure_table\n");
        return 0;
    }
    /* Set parent entry: child phys | present | rw */
    uint64_t newent = (child & PTE_ADDR_MASK) | PTE_PRESENT | PTE_RW;
    write_entry(parent_phys, index, newent);
    return child;
}

/* Map a single 4 KiB page (creates intermediate tables as needed). */
int paging_map_page(uintptr_t pml4_phys, uint64_t vaddr, uintptr_t paddr, uint64_t flags) {
    if (!pml4_phys) return -1;

    uint64_t i4 = pml4_index(vaddr);
    uint64_t i3 = pdpt_index(vaddr);
    uint64_t i2 = pd_index(vaddr);
    uint64_t i1 = pt_index(vaddr);

    uintptr_t pdpt_phys = ensure_table(pml4_phys, i4);
    if (!pdpt_phys) {
        uart_puts("paging: ensure pdpt failed\n");
        return -1;
    }
    uintptr_t pd_phys = ensure_table(pdpt_phys, i3);
    if (!pd_phys) {
        uart_puts("paging: ensure pd failed\n");
        return -1;
    }

    uintptr_t pt_phys = ensure_table(pd_phys, i2);
    if (!pt_phys) {
        uart_puts("paging: ensure pt failed\n");
        return -1;
    }

    uint64_t entry = (paddr & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK);
    entry |= PTE_PRESENT; /* ensure present */
    write_entry(pt_phys, i1, entry);
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

/* Unmap consecutive pages (n_pages). */
int paging_unmap_range(uintptr_t pml4_phys, uint64_t vaddr, size_t n_pages) {
    for (size_t i = 0; i < n_pages; i++) {
        int r = paging_unmap_page(pml4_phys, vaddr + (i << PAGE_SHIFT));
        if (r && r != -2 && r != -3 && r != -4) {
            /* Skip silently if intermediate tables missing; otherwise return error. */
            return r;
        }
    }
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


uintptr_t paging_get_current_cr3() {
    uintptr_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/*
 * paging_setup_kernel_pml4()
 *
 * Sets up the kernel's initial PML4 with identity mappings for:
 *   1. Kernel code/data/stack (kernel_start to kernel_end)
 *   2. Early I/O region (0 to 4 MiB)
 *   3. GOP framebuffer (0x80000000, assuming 1024x768 ~3 MiB, map 4 MiB)
 *
 * Returns physical address of the kernel PML4, or 0 on failure.
 *
 * NOTE: Assumes identity mapping (virt == phys, phys_map_offset == 0).
 * If you change phys_map_offset, kernel page tables must account for that.
 */

extern uintptr_t kernel_start;  /* from kernel.ld */
extern uintptr_t kernel_end;    /* from kernel.ld */

#define FRAMEBUFFER_PADDR  0x80000000ULL
#define FRAMEBUFFER_SIZE   (4 * 1024 * 1024)  /* 4 MiB to be safe */
#define EARLY_IO_SIZE      (4 * 1024 * 1024)  /* Minimal low window for early identity access */

uintptr_t paging_setup_kernel_pml4(void) {
    /* Create kernel PML4 */
    uintptr_t pml4 = paging_create_pml4();
    if (!pml4) return 0;

    uart_puts("paging: pml4 allocated\n");

    /* Identity-map kernel region (kernel_start to kernel_end) */
    uintptr_t kstart = (uintptr_t)&kernel_start;
    uintptr_t kend   = (uintptr_t)&kernel_end;
    
    /* Align down to page boundary for start */
    uintptr_t kstart_aligned = kstart & ~(PAGE_SIZE - 1);
    /* Align up to page boundary for end */
    uintptr_t kend_aligned = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    size_t n_kernel_pages = (kend_aligned - kstart_aligned) / PAGE_SIZE;
    
    const uint64_t k_flags = PTE_PRESENT | PTE_RW | PTE_GLOBAL;

    if (paging_map_range(pml4, kstart_aligned, kstart_aligned,
                         n_kernel_pages, k_flags)) {
        uart_puts("paging: map kernel failed\n");
        return 0;  /* fail */
    }

    /* Higher-half alias for the kernel region */
    uintptr_t kvirt_base = KERNEL_VIRT_BASE + kstart_aligned;
    if (paging_map_range(pml4, kvirt_base, kstart_aligned,
                         n_kernel_pages, k_flags)) {
        uart_puts("paging: map higher-half kernel failed\n");
        return 0;
    }

    /* Identity-map early I/O region (0 to 4 MiB) */
    size_t n_io_pages = EARLY_IO_SIZE / PAGE_SIZE;
    if (paging_map_range(pml4, 0, 0, n_io_pages, PTE_PRESENT | PTE_RW | PTE_GLOBAL)) {
        uart_puts("paging: map io failed\n");
        return 0;
    }

    /* Identity-map GOP framebuffer (0x80000000 for ~4 MiB) */
    size_t n_fb_pages = (FRAMEBUFFER_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (paging_map_range(pml4, FRAMEBUFFER_PADDR, FRAMEBUFFER_PADDR,
                         n_fb_pages, PTE_PRESENT | PTE_RW | PTE_GLOBAL)) {
        uart_puts("paging: map fb failed\n");
        return 0;
    }

    uart_puts("paging: kernel pml4 ready\n");
    return pml4;
}