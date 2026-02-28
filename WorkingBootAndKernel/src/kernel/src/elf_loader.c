#include "elf_loader.h"
#include "paging.h"
#include "frame_alloc.h"
#include <uart.h>
#include <stdint.h>

#include "elf.h"   // You need to define Elf64_Ehdr, Elf64_Phdr

int elf_load_executable(const void* elf_file_data, uintptr_t pml4_phys, uintptr_t* entry_out)
{
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_file_data;

    // Verify ELF magic
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') 
    {
        uart_puts("ELF: Invalid magic\n");
        return -1;
    }

    *entry_out = ehdr->e_entry;

    Elf64_Phdr* phdr = (Elf64_Phdr*)((uint8_t*)elf_file_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uintptr_t vaddr = phdr[i].p_vaddr;
        uintptr_t filesz = phdr[i].p_filesz;
        uintptr_t memsz  = phdr[i].p_memsz;
        uint8_t* src     = (uint8_t*)elf_file_data + phdr[i].p_offset;

        // Allocate + map each page
        for (uintptr_t off = 0; off < memsz; off += 0x1000)
        {
            uintptr_t page_vaddr = (vaddr + off) & ~0xFFF;
            uintptr_t frame = pfa_alloc_frame();

            if (!frame)
            {
                uart_puts("ELF: OOM\n");
                return -2;
            }

            paging_map_page(pml4_phys, page_vaddr, frame, PTE_PRESENT | PTE_RW | PTE_USER);
        }

        // Copy filesz
        memcpy((void*)vaddr, src, filesz);

        // Zero BSS
        memset((void*)(vaddr + filesz), 0, memsz - filesz);
    }

    return 0;
}
