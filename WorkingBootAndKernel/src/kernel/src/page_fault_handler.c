#include "page_fault_handler.h"
#include <uart.h>
#include <stdint.h>
#include "paging.h"
#include "frame_alloc.h"

/*
Error code bits:
bit 0: P = 0 page not present, 1 protection violation
bit 1: W/R = 1 write fault, 0 read fault
bit 2: U/S = 1 user-mode, 0 kernel-mode
bit 3: RSVD = reserved bits overwritten
bit 4: I/D = instruction fetch
*/

static void decode_error(uint64_t code)
{
    uart_puts("  Present:      "); uart_putu(code & 1); uart_putchar('\n');
    uart_puts("  Write:        "); uart_putu((code >> 1) & 1); uart_putchar('\n');
    uart_puts("  User:         "); uart_putu((code >> 2) & 1); uart_putchar('\n');
    uart_puts("  Reserved bit: "); uart_putu((code >> 3) & 1); uart_putchar('\n');
    uart_puts("  Instr fetch:  "); uart_putu((code >> 4) & 1); uart_putchar('\n');
}

int page_fault_handler(uint64_t error_code, uint64_t faulting_address)
{
    uart_puts("\n===== PAGE FAULT =====\n");
    uart_puts("Faulting address: 0x"); uart_puthex(faulting_address); uart_putchar('\n');
    uart_puts("Error code:       0x"); uart_puthex(error_code); uart_putchar('\n');

    decode_error(error_code);

    // If the page was not present – handle demand paging
    if ((error_code & PF_CAUSE_PRESENT) == 0)
    {
        uart_puts("Page not present → invoking handler...\n");
        int r = page_not_present_handler(faulting_address);

        if (r == 0)
        {
            uart_puts("Page fault resolved.\n");
            return 0; // continue execution
        }

        uart_puts("page_not_present_handler FAILED!\n");
    }
    else
    {
        uart_puts("Protection fault! (Present=1)\n");
    }

    uart_puts("FATAL: System halted.\n");
    while (1) { __asm__("hlt"); }
    return -1;
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

