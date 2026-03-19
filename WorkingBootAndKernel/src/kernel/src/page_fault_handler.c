#include "page_fault_handler.h"
#include "include/panic.h"
#include <uart.h>
#include <stdint.h>
#include "paging.h"
#include "frame_alloc.h"
#include "refcount.h"

/*
Error code bits:
bit 0: P = 0 page not present, 1 protection violation
bit 1: W/R = 1 write fault, 0 read fault
bit 2: U/S = 1 user-mode, 0 kernel-mode
bit 3: RSVD = reserved bits overwritten
bit 4: I/D = instruction fetch
*/

int page_fault_handler(uint64_t error_code, uint64_t faulting_address, regs_t *regs)
{
    uart_puts("#PF cr2=0x");
    uart_puthex(faulting_address);
    uart_puts(" err=0x");
    uart_puthex(error_code);
    uart_puts("\n");

    /* Check if this is a write fault on a present page (potential COW) */
    if ((error_code & PF_CAUSE_PRESENT) && (error_code & PF_CAUSE_WRITE)) {
        uintptr_t pml4_phys = paging_get_current_cr3();
        uint64_t flags = paging_get_flags(pml4_phys, faulting_address);
        
        if (flags & PTE_COW) {
            uart_puts("COW page detected - handling copy-on-write...\n");
            int r = paging_handle_cow_fault(pml4_phys, faulting_address);
            
            if (r == 0) {
                uart_puts("COW fault resolved successfully.\n");
                return 0;
            }
            
            kernel_panic("COW fault handler failed", regs, faulting_address);
        }
        
        kernel_panic("Protection fault (write to non-COW page)", regs, faulting_address);
    }

    /* Page not present — handle demand paging */
    if ((error_code & PF_CAUSE_PRESENT) == 0)
    {
        int r = page_not_present_handler(faulting_address);

        if (r == 0)
        {
            uart_puts("Page fault resolved.\n");
            return 0;
        }

        kernel_panic("Page not present — handler failed", regs, faulting_address);
    }

    /* Protection fault that isn't a write-on-present (e.g. read of supervisor page) */
    kernel_panic("Protection fault", regs, faulting_address);
    return -1;  /* unreachable */
}


int page_not_present_handler(uint64_t faulting_address)
{
    // align to 4 KiB page
    uint64_t page_addr = faulting_address & ~0xFFFULL;

    uintptr_t phys = pfa_alloc_frame();
    if (!phys)
    {
        uart_puts("OOM: cannot allocate frame\n");
        return -1;
    }

    // TODO: replace with your actual CR3 loader or per-process PML4
    uintptr_t pml4_phys = paging_get_current_cr3();

    if (!pml4_phys)
    {
        uart_puts("ERROR: CR3 not initialized!\n");
        return -1;
    }

    int r = paging_map_page(
        pml4_phys,
        page_addr,
        phys,
        PTE_PRESENT | PTE_RW | PTE_USER
    );

    if (r != 0)
    {
        uart_puts("Failed to map page\n");
        return -2;
    }

    uart_puts("Mapped missing page: 0x");
    uart_puthex(faulting_address);
    uart_putchar('\n');

    return 0;
}

