/* ==========================================================
 * File: page_fault_handler_mm.c
 * Purpose: page fault handler integration that consults current_mm
 * and the VMA list to demand-allocate pages with proper flags.
 * ==========================================================
 */

#include "page_fault_handler.h"
#include "mm.h"
#include "paging.h"
#include "uart.h"

extern mm_struct* current_mm; /* from mm.c */

int page_not_present_handler_mm(uint64_t faulting_address)
{
    if (!current_mm) {
        uart_puts("PF: no current_mm\n");
        return -1;
    }

    uintptr_t page_addr = faulting_address & ~0xFFFULL;

    vm_area_struct* v = mm_find_vma(current_mm, faulting_address);
    if (!v) {
        uart_puts("PF: no VMA for address\n");
        return -1;
    }

    uintptr_t phys = pfa_alloc_frame();
    if (!phys) {
        uart_puts("PF: OOM allocating frame\n");
        return -1;
    }

    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (v->flags & VM_WRITE) flags |= PTE_RW;

    int r = paging_map_page(current_mm->pml4_phys, page_addr, phys, flags);
    if (r != 0) {
        uart_puts("PF: failed to map page\n");
        return -2;
    }

    /* Zero the newly allocated physical page via phys->virt mapping if available */
    void* vptr = (void*)page_addr;
    memset(vptr, 0, PAGE_SIZE);

    return 0;
}
