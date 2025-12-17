/* ==========================================================
 * File: elf_loader_mm.c
 * Purpose: Modified ELF loader that registers VMAs into mm_struct
 * and copies/zeros segments into the process address space.
 * NOTE: This loader assumes the kernel uses a phys->virt mapping
 *       where physical addresses can be accessed via phys + offset
 *       as implemented in your paging.c via phys_map_offset.
 *       If you load user pages into the identity-higher-half,
 *       you must map or copy accordingly. This implementation
 *       maps frames and writes directly to the virtual address
 *       (which must be accessible in the current address space).
 * ==========================================================
 */

#include "elf_loader.h"
#include "elf.h"
#include "mm.h"
#include "paging.h"
#include "frame_alloc.h"
#include "uart.h"
#include "string.h" /* your kernel memcpy/memset */

int elf_load_executable_mm(const void* elf_file_data, mm_struct* mm, uintptr_t* entry_out)
{
    if (!elf_file_data || !mm) return -1;

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_file_data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        uart_puts("ELF: Invalid magic\n");
        return -1;
    }

    *entry_out = ehdr->e_entry;

    Elf64_Phdr* phdr = (Elf64_Phdr*)((uint8_t*)elf_file_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uintptr_t vaddr = (uintptr_t)phdr[i].p_vaddr;
        uintptr_t filesz = (uintptr_t)phdr[i].p_filesz;
        uintptr_t memsz  = (uintptr_t)phdr[i].p_memsz;
        uint8_t* src     = (uint8_t*)elf_file_data + phdr[i].p_offset;

        /* register VMA */
        uint32_t vmflags = 0;
        if (phdr[i].p_flags & PF_R) vmflags |= VM_READ;
        if (phdr[i].p_flags & PF_W) vmflags |= VM_WRITE;
        if (phdr[i].p_flags & PF_X) vmflags |= VM_EXEC;

        mm_add_vma(mm, vaddr, vaddr + memsz, vmflags);

        /* allocate and map pages for the segment */
        for (uintptr_t off = 0; off < memsz; off += PAGE_SIZE) {
            uintptr_t page_vaddr = (vaddr + off) & ~(PAGE_SIZE - 1);
            uintptr_t frame = pfa_alloc_frame();
            if (!frame) {
                uart_puts("ELF: OOM allocating frame for segment\n");
                return -2;
            }

            /* Map with permissions derived from VMA flags */
            uint64_t pte_flags = PTE_PRESENT | PTE_USER;
            if (vmflags & VM_WRITE) pte_flags |= PTE_RW;
            /* NOTE: we don't model NX in this simple example */

            int r = paging_map_page(mm->pml4_phys, page_vaddr, frame, pte_flags);
            if (r != 0) {
                uart_puts("ELF: paging_map_page failed\n");
                return -3;
            }

            /* Copy file data (if this page intersects filesz) and zero rest */
            uintptr_t seg_off = off;
            uintptr_t to_copy = 0;
            if (seg_off < filesz) {
                uintptr_t avail = filesz - seg_off;
                to_copy = (avail > PAGE_SIZE) ? PAGE_SIZE : avail;
            }

            /* Compute destination virtual pointer: depends on kernel's phys->virt mapping.
               For simplicity we assume kernel's current address space maps the target
               virtual addresses (e.g. higher-half mapping). If not, you must temporarily
               map the PML4 or use phys_to_virt(frame) + offset. */

            void* dst = (void*)(page_vaddr);
            if (to_copy)
                memcpy(dst, src + seg_off, to_copy);

            if (to_copy < PAGE_SIZE)
                memset((uint8_t*)dst + to_copy, 0, PAGE_SIZE - to_copy);
        }
    }

    return 0;
}