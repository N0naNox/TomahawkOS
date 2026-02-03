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
#include "refcount.h"

/* Kernel boundaries from linker script */
extern char kernel_start;
extern char kernel_end;
/* phys->virt mapping offset (virt = phys + phys_map_offset) */
static uintptr_t g_phys_map_offset = 0;

/* Higher-half base for the kernel mapping (virt alias of the physical kernel). */
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#define PAGE_USER (1ULL << 2)



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
   Returns physical address of child table (present) or 0 on failure.
   Propagates user flag if requested. */
static uintptr_t ensure_table_with_flags(uintptr_t parent_phys, uint64_t index, uint64_t flags) {
    uint64_t ent = read_entry(parent_phys, index);
    if (ent & PTE_PRESENT) {
        /* Table exists - update flags if needed */
        if (flags & PTE_USER) {
            /* Ensure USER bit is set at this level */
            ent |= PTE_USER;
            write_entry(parent_phys, index, ent);
        }
        return (uintptr_t)(ent & PTE_ADDR_MASK);
    }
    uintptr_t child = alloc_zeroed_page();
    if (!child) {
        uart_puts("paging: alloc_zeroed_page failed in ensure_table\n");
        return 0;
    }
    /* Set parent entry: child phys | present | rw | propagate user flag */
    uint64_t newent = (child & PTE_ADDR_MASK) | PTE_PRESENT | PTE_RW;
    if (flags & PTE_USER) {
        newent |= PTE_USER;
    }
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

    uintptr_t pdpt_phys = ensure_table_with_flags(pml4_phys, i4, flags);
    if (!pdpt_phys) {
        uart_puts("paging: ensure pdpt failed\n");
        return -1;
    }
    uintptr_t pd_phys = ensure_table_with_flags(pdpt_phys, i3, flags);
    if (!pd_phys) {
        uart_puts("paging: ensure pd failed\n");
        return -1;
    }

    uintptr_t pt_phys = ensure_table_with_flags(pd_phys, i2, flags);
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

#define FRAMEBUFFER_PADDR  0x80000000ULL
#define FRAMEBUFFER_SIZE   (4 * 1024 * 1024)  /* 4 MiB to be safe */
#define EARLY_IO_SIZE      (512 * 1024 * 1024)  /* 512 MiB - enough for kernel heap/allocations */

uintptr_t paging_setup_kernel_pml4(void) {
    /* Create kernel PML4 */
    uintptr_t pml4 = paging_create_pml4();
    if (!pml4) return 0;

    uart_puts("paging: pml4 allocated\n");

    /* Get kernel boundaries */
    uintptr_t kstart = (uintptr_t)&kernel_start;
    uintptr_t kend   = (uintptr_t)&kernel_end;
    
    /* Align down to page boundary for start */
    uintptr_t kstart_aligned = kstart & ~(PAGE_SIZE - 1);
    /* Align up to page boundary for end */
    uintptr_t kend_aligned = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    size_t n_kernel_pages = (kend_aligned - kstart_aligned) / PAGE_SIZE;
    
    const uint64_t k_flags = PTE_PRESENT | PTE_RW | PTE_GLOBAL;

    /* ONLY create identity mapping temporarily - we'll remove it after jumping to higher-half */
    if (paging_map_range(pml4, kstart_aligned, kstart_aligned,
                         n_kernel_pages, k_flags)) {
        uart_puts("paging: map kernel failed\n");
        return 0;  /* fail */
    }

    /* Higher-half mapping for the kernel region - this is the PERMANENT mapping */
    uintptr_t kvirt_base = KERNEL_VIRT_BASE + kstart_aligned;
    if (paging_map_range(pml4, kvirt_base, kstart_aligned,
                         n_kernel_pages, k_flags)) {
        uart_puts("paging: map higher-half kernel failed\n");
        return 0;
    }

    /* Identity-map early I/O region (0 to 4 MiB) - needed for VGA, UART, etc */
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

/* Remove the identity mapping of the kernel code after jumping to higher-half.
 * This frees up low memory (1MB-4MB range) for user programs. */
void paging_remove_kernel_identity_map(uintptr_t pml4_phys) {
    extern char kernel_start;
    extern char kernel_end;
    
    uintptr_t kstart = (uintptr_t)&kernel_start;
    uintptr_t kend   = (uintptr_t)&kernel_end;
    
    uintptr_t kstart_aligned = kstart & ~(PAGE_SIZE - 1);
    uintptr_t kend_aligned = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    size_t n_kernel_pages = (kend_aligned - kstart_aligned) / PAGE_SIZE;
    
    uart_puts("paging: removing kernel identity map from 0x");
    uart_puthex(kstart_aligned);
    uart_puts(" (");
    uart_putu(n_kernel_pages);
    uart_puts(" pages)...\n");
    
    /* Unmap the kernel's identity mapping (low addresses) */
    paging_unmap_range(pml4_phys, kstart_aligned, n_kernel_pages);
    
    /* Flush TLB by reloading CR3 */
    paging_load_cr3(pml4_phys);
    
    uart_puts("paging: identity map removed, low memory freed for user programs\n");
}


void paging_set_user_bit(uintptr_t virt_addr, int enable) {
    // השגנו את ה-PML4 הנוכחי מ-CR3
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)(cr3 & ~0xFFFULL);

    // פירוק הכתובת הוירטואלית לאינדקסים
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF;

    // PML4 -> PDPT
    if (!(pml4[pml4_idx] & 1)) return; // דף לא קיים
    pml4[pml4_idx] |= PAGE_USER;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

    // PDPT -> PD
    if (!(pdpt[pdpt_idx] & 1)) return;
    pdpt[pdpt_idx] |= PAGE_USER;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    // PD -> PT
    if (!(pd[pd_idx] & 1)) return;
    pd[pd_idx] |= PAGE_USER;
    
    // אם זה דף גדול (2MB), עצרנו כאן
    if (pd[pd_idx] & (1ULL << 7)) return;

    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);

    // PT -> Page
    if (!(pt[pt_idx] & 1)) return;
    pt[pt_idx] |= PAGE_USER;

    // רענון ה-TLB כדי שהשינוי ייקלט
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

/* Get the PTE pointer for a virtual address */
uint64_t* paging_get_pte(uintptr_t pml4_phys, uint64_t vaddr) {
    if (!pml4_phys) return NULL;

    uint64_t i4 = pml4_index(vaddr);
    uint64_t i3 = pdpt_index(vaddr);
    uint64_t i2 = pd_index(vaddr);
    uint64_t i1 = pt_index(vaddr);

    uint64_t ent4 = read_entry(pml4_phys, i4);
    if (!(ent4 & PTE_PRESENT)) return NULL;
    uintptr_t pdpt = (uintptr_t)(ent4 & PTE_ADDR_MASK);

    uint64_t ent3 = read_entry(pdpt, i3);
    if (!(ent3 & PTE_PRESENT)) return NULL;
    if (ent3 & PTE_PS) return NULL; /* 1 GiB page - not supported for PTE access */
    
    uintptr_t pd = (uintptr_t)(ent3 & PTE_ADDR_MASK);
    uint64_t ent2 = read_entry(pd, i2);
    if (!(ent2 & PTE_PRESENT)) return NULL;
    if (ent2 & PTE_PS) return NULL; /* 2 MiB page - not supported for PTE access */

    uintptr_t pt = (uintptr_t)(ent2 & PTE_ADDR_MASK);
    uint64_t* pt_virt = (uint64_t*)phys_to_virt(pt);
    return &pt_virt[i1];
}

/* Get PTE flags for a virtual address */
uint64_t paging_get_flags(uintptr_t pml4_phys, uint64_t vaddr) {
    uint64_t* pte = paging_get_pte(pml4_phys, vaddr);
    if (!pte) return 0;
    return *pte & ~PTE_ADDR_MASK;
}

/* Set flags for a mapped page */
int paging_set_flags(uintptr_t pml4_phys, uint64_t vaddr, uint64_t flags) {
    uint64_t* pte = paging_get_pte(pml4_phys, vaddr);
    if (!pte) return -1;
    
    *pte |= (flags & ~PTE_ADDR_MASK);
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

/* Clear flags for a mapped page */
int paging_clear_flags(uintptr_t pml4_phys, uint64_t vaddr, uint64_t flags) {
    uint64_t* pte = paging_get_pte(pml4_phys, vaddr);
    if (!pte) return -1;
    
    *pte &= ~(flags & ~PTE_ADDR_MASK);
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

/* Mark a page range as COW (read-only + COW flag) */
int paging_mark_cow(uintptr_t pml4_phys, uint64_t vaddr, size_t n_pages) {
    for (size_t i = 0; i < n_pages; i++) {
        uint64_t page_addr = vaddr + (i << PAGE_SHIFT);
        uint64_t* pte = paging_get_pte(pml4_phys, page_addr);
        
        if (!pte || !(*pte & PTE_PRESENT)) continue;
        
        /* Mark as read-only and COW */
        *pte &= ~PTE_RW;
        *pte |= PTE_COW;
        __asm__ volatile("invlpg (%0)" :: "r"(page_addr) : "memory");
    }
    return 0;
}

/* Handle COW page fault */
int paging_handle_cow_fault(uintptr_t pml4_phys, uint64_t vaddr) {
    uint64_t page_addr = vaddr & ~(PAGE_SIZE - 1);
    uint64_t* pte = paging_get_pte(pml4_phys, page_addr);
    
    if (!pte || !(*pte & PTE_PRESENT)) {
        uart_puts("COW fault: page not present\n");
        return -1;
    }
    
    if (!(*pte & PTE_COW)) {
        uart_puts("COW fault: not a COW page\n");
        return -2;
    }
    
    uintptr_t old_phys = *pte & PTE_ADDR_MASK;
    
    /* Check if page is shared */
    if (refcount_is_shared(old_phys)) {
        /* Page is shared - need to copy */
        uart_puts("COW: copying shared page\n");
        
        /* Allocate new physical page */
        uintptr_t new_phys = pfa_alloc_frame();
        if (!new_phys) {
            uart_puts("COW: out of memory\n");
            return -3;
        }
        
        /* Copy page content */
        void* old_virt = phys_to_virt(old_phys);
        void* new_virt = phys_to_virt(new_phys);
        uint8_t* src = (uint8_t*)old_virt;
        uint8_t* dst = (uint8_t*)new_virt;
        for (size_t i = 0; i < PAGE_SIZE; i++) {
            dst[i] = src[i];
        }
        
        /* Decrement refcount for old page */
        uint32_t old_refs = refcount_dec(old_phys);
        if (old_refs == 0) {
            /* Old page no longer referenced - free it */
            pfa_free_frame(old_phys);
        }
        
        /* Set refcount for new page */
        refcount_set(new_phys, 1);
        
        /* Update PTE to point to new page, mark writable, clear COW */
        *pte = (new_phys & PTE_ADDR_MASK) | (*pte & ~PTE_ADDR_MASK);
        *pte |= PTE_RW;
        *pte &= ~PTE_COW;
        
        uart_puts("COW: page copied successfully\n");
    } else {
        /* Page not shared - just mark as writable */
        uart_puts("COW: making page writable (not shared)\n");
        *pte |= PTE_RW;
        *pte &= ~PTE_COW;
    }
    
    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" :: "r"(page_addr) : "memory");
    
    return 0;
}

/* Clone page tables with COW */
uintptr_t paging_clone_cow(uintptr_t src_pml4_phys) {
    if (!src_pml4_phys) return 0;
    
    /* Create new PML4 */
    uintptr_t dst_pml4_phys = paging_create_pml4();
    if (!dst_pml4_phys) {
        uart_puts("COW clone: failed to create PML4\n");
        return 0;
    }
    
    uint64_t* src_pml4 = (uint64_t*)phys_to_virt(src_pml4_phys);
    uint64_t* dst_pml4 = (uint64_t*)phys_to_virt(dst_pml4_phys);
    
    /* Walk through PML4 entries */
    for (int i4 = 0; i4 < 512; i4++) {
        if (!(src_pml4[i4] & PTE_PRESENT)) continue;
        
        /* Skip kernel mappings (upper half) - these should be shared directly */
        if (i4 >= 256) {
            dst_pml4[i4] = src_pml4[i4];
            continue;
        }
        
        uintptr_t src_pdpt = src_pml4[i4] & PTE_ADDR_MASK;
        uintptr_t dst_pdpt = alloc_zeroed_page();
        if (!dst_pdpt) {
            uart_puts("COW clone: failed to alloc PDPT\n");
            return 0;
        }
        
        dst_pml4[i4] = (dst_pdpt & PTE_ADDR_MASK) | (src_pml4[i4] & ~PTE_ADDR_MASK);
        
        uint64_t* src_pdpt_ptr = (uint64_t*)phys_to_virt(src_pdpt);
        uint64_t* dst_pdpt_ptr = (uint64_t*)phys_to_virt(dst_pdpt);
        
        /* Walk PDPT entries */
        for (int i3 = 0; i3 < 512; i3++) {
            if (!(src_pdpt_ptr[i3] & PTE_PRESENT)) continue;
            if (src_pdpt_ptr[i3] & PTE_PS) {
                /* 1GB page - just copy entry */
                dst_pdpt_ptr[i3] = src_pdpt_ptr[i3];
                continue;
            }
            
            uintptr_t src_pd = src_pdpt_ptr[i3] & PTE_ADDR_MASK;
            uintptr_t dst_pd = alloc_zeroed_page();
            if (!dst_pd) {
                uart_puts("COW clone: failed to alloc PD\n");
                return 0;
            }
            
            dst_pdpt_ptr[i3] = (dst_pd & PTE_ADDR_MASK) | (src_pdpt_ptr[i3] & ~PTE_ADDR_MASK);
            
            uint64_t* src_pd_ptr = (uint64_t*)phys_to_virt(src_pd);
            uint64_t* dst_pd_ptr = (uint64_t*)phys_to_virt(dst_pd);
            
            /* Walk PD entries */
            for (int i2 = 0; i2 < 512; i2++) {
                if (!(src_pd_ptr[i2] & PTE_PRESENT)) continue;
                if (src_pd_ptr[i2] & PTE_PS) {
                    /* 2MB page - just copy entry */
                    dst_pd_ptr[i2] = src_pd_ptr[i2];
                    continue;
                }
                
                uintptr_t src_pt = src_pd_ptr[i2] & PTE_ADDR_MASK;
                uintptr_t dst_pt = alloc_zeroed_page();
                if (!dst_pt) {
                    uart_puts("COW clone: failed to alloc PT\n");
                    return 0;
                }
                
                dst_pd_ptr[i2] = (dst_pt & PTE_ADDR_MASK) | (src_pd_ptr[i2] & ~PTE_ADDR_MASK);
                
                uint64_t* src_pt_ptr = (uint64_t*)phys_to_virt(src_pt);
                uint64_t* dst_pt_ptr = (uint64_t*)phys_to_virt(dst_pt);
                
                /* Walk PT entries (actual pages) */
                for (int i1 = 0; i1 < 512; i1++) {
                    if (!(src_pt_ptr[i1] & PTE_PRESENT)) continue;
                    
                    uintptr_t phys = src_pt_ptr[i1] & PTE_ADDR_MASK;
                    
                    /* Check if this is kernel memory (0x100000 to kernel_end) */
                    uintptr_t kernel_end_addr = (uintptr_t)&kernel_end;
                    int is_kernel = (phys >= 0x100000 && phys < kernel_end_addr);
                    
                    /* Copy entry to destination */
                    dst_pt_ptr[i1] = src_pt_ptr[i1];
                    
                    /* If page is writable AND not kernel memory, make it COW */
                    if ((src_pt_ptr[i1] & PTE_RW) && !is_kernel) {
                        src_pt_ptr[i1] &= ~PTE_RW;
                        src_pt_ptr[i1] |= PTE_COW;
                        
                        dst_pt_ptr[i1] &= ~PTE_RW;
                        dst_pt_ptr[i1] |= PTE_COW;
                        
                        /* Increment refcount for shared page */
                        refcount_inc(phys);
                        refcount_inc(phys); /* Once for each mapping */
                    } else if (is_kernel) {
                        /* Kernel pages stay shared and writable - increment refcount once */
                        refcount_inc(phys);
                    }
                }
            }
        }
    }
    
    uart_puts("COW clone: page tables cloned successfully\n");
    return dst_pml4_phys;
}