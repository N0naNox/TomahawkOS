#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

/* Page constants */
#define PAGE_SIZE    4096
#define PAGE_SHIFT   12

/* Index helpers (4-level x86_64: PML4, PDPT, PD, PT) */
static inline uint64_t pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t pd_index(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline uint64_t pt_index(uint64_t va)   { return (va >> 12) & 0x1FF; }
static inline uint64_t page_offset(uint64_t va) { return va & 0xFFF; }

/* PTE flags */
#define PTE_PRESENT    (1ULL << 0)
#define PTE_RW         (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_PWT        (1ULL << 3)
#define PTE_PCD        (1ULL << 4)
#define PTE_ACCESSED   (1ULL << 5)
#define PTE_DIRTY      (1ULL << 6)
#define PTE_PS         (1ULL << 7)   /* Page Size (for PD/PDPT) */
#define PTE_GLOBAL     (1ULL << 8)
#define PTE_COW        (1ULL << 9)   /* Copy-On-Write (OS-specific bit) */
#define PTE_NO_EXECUTE (1ULL << 63)  /* NX bit (if supported) */

/* Mask to extract physical address portion from an entry */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/* Initialize the paging helper.
   phys_map_offset: add this offset to a physical address to obtain a kernel
   virtual pointer (virt = phys + phys_map_offset). Pass 0 if physical==virtual. */
void paging_init(uintptr_t phys_map_offset);

/* Create a new PML4 page (returns physical address of PML4 or 0 on failure) */
uintptr_t paging_create_pml4(void);

/* Map a single 4 KiB page. Returns 0 on success, non-zero on error. */
int paging_map_page(uintptr_t pml4_phys, uint64_t vaddr, uintptr_t paddr, uint64_t flags);

/* Map consecutive pages (n_pages). Returns 0 on success. */
int paging_map_range(uintptr_t pml4_phys, uint64_t vaddr, uintptr_t paddr, size_t n_pages, uint64_t flags);

/* Unmap one page (clears PT entry). Does not free page-table pages. */
int paging_unmap_page(uintptr_t pml4_phys, uint64_t vaddr);

/* Unmap consecutive pages. */
int paging_unmap_range(uintptr_t pml4_phys, uint64_t vaddr, size_t n_pages);

// Get current CR3 (PML4 physical address)
uintptr_t paging_get_current_cr3();

/* Lookup: return physical address mapped to vaddr, or 0 if not mapped. Handles large pages. */
uintptr_t paging_get_phys(uintptr_t pml4_phys, uint64_t vaddr);

/* Load CR3 with the provided PML4 physical address (switch page tables). */
static inline void paging_load_cr3(uintptr_t pml4_phys) {
    asm volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

/* Optional helper to release a PML4 (not implemented automatically) */
void paging_free_pml4(uintptr_t pml4_phys);

/* Set up kernel PML4 with identity mappings for kernel code, early I/O, and framebuffer.
   fb_paddr and fb_size specify the GOP framebuffer region to map.
   Returns physical address of kernel PML4, or 0 on failure. */
uintptr_t paging_setup_kernel_pml4(uintptr_t fb_paddr, size_t fb_size);

/* Remove the temporary identity mapping of the kernel after jumping to higher-half */
void paging_remove_kernel_identity_map(uintptr_t pml4_phys);

/* Get the PTE for a virtual address (returns pointer to entry or NULL if not mapped) */
uint64_t* paging_get_pte(uintptr_t pml4_phys, uint64_t vaddr);

/* Get PTE flags for a virtual address (0 if not mapped) */
uint64_t paging_get_flags(uintptr_t pml4_phys, uint64_t vaddr);

/* Set/clear specific flags for a mapped page */
int paging_set_flags(uintptr_t pml4_phys, uint64_t vaddr, uint64_t flags);
int paging_clear_flags(uintptr_t pml4_phys, uint64_t vaddr, uint64_t flags);

/* Clone page tables with COW (Copy-On-Write) mode.
 * Copies page table structure, marks pages as read-only and COW,
 * increments reference counts for shared physical pages.
 * Returns new PML4 physical address or 0 on failure. */
uintptr_t paging_clone_cow(uintptr_t src_pml4_phys);

/* Mark a page range as read-only and COW */
int paging_mark_cow(uintptr_t pml4_phys, uint64_t vaddr, size_t n_pages);

/* Handle COW page fault: copy page if shared, or just mark writable if not */
int paging_handle_cow_fault(uintptr_t pml4_phys, uint64_t vaddr);

/* Set user bit for a virtual address (legacy function) */
void paging_set_user_bit(uintptr_t virt_addr, int enable);

#endif /* PAGING_H */