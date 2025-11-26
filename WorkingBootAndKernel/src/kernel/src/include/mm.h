/* ==========================================================
 * File: mm.h
 * Purpose: mm_struct + VMA definitions and public API
 * ==========================================================
 */

#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stddef.h>

/* VMA permission flags (simple) */
#define VM_READ   0x1
#define VM_WRITE  0x2
#define VM_EXEC   0x4

typedef struct vm_area_struct {
    uintptr_t start;       /* inclusive */
    uintptr_t end;         /* exclusive */
    uint32_t  flags;       /* VM_READ/VM_WRITE/VM_EXEC */
    struct vm_area_struct* next;
} vm_area_struct;

typedef struct mm_struct {
    uintptr_t pml4_phys;           /* physical address of PML4 (CR3) */
    vm_area_struct* mmap;          /* linked list of VMAs */
} mm_struct;

/* Create/free address space */
mm_struct* mm_create(void);
void mm_destroy(mm_struct* mm);

/* Add / find VMA */
int mm_add_vma(mm_struct* mm, uintptr_t start, uintptr_t end, uint32_t flags);
vm_area_struct* mm_find_vma(mm_struct* mm, uintptr_t addr);

/* Switch to an address space (load CR3) */
void mm_switch(mm_struct* mm);

/* Current mm (set by scheduler / loader) */
extern mm_struct* current_mm;

#endif /* MM_H */


/* ==========================================================
 * File: mm.c
 * Purpose: Implementation of mm_struct, VMA list, simple allocator
 * Note: This implements a tiny bump allocator used to allocate VMAs
 *       for demonstration purposes. Replace kmalloc/kmfree with
 *       your kernel allocator when available.
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

/* Exported current mm */n
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
    /* NOTE: we don't free page tables or frames here (placeholder) */
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

/* End of address space implementation files */
