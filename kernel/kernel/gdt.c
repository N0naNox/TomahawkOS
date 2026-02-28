#include "gdt.h"

// 3 entries: null, code, data
static struct GDTEntry gdt[3];
static struct GDTPtr gdt_ptr;

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_middle = (base >> 16) & 0xFF;
    gdt[i].access = access;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init()
{
    // Pointer to GDT
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    // Null
    set_entry(0, 0, 0, 0, 0);

    // Kernel code
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);

    // Kernel data
    set_entry(2, 0, 0xFFFFF, 0x92, 0xA0);
}

extern void gdt_load_asm(struct GDTPtr*);

void gdt_load()
{
    gdt_init();
    gdt_load_asm(&gdt_ptr);
}