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


