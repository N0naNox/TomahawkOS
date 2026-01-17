#include "include/gdt.h"
#include <string.h>

// A 64-bit GDT entry (simplified)
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));


static struct gdt_entry gdt[7];
static struct gdt_ptr g_gdtr;
static struct tss_entry g_tss;


void write_tss(int num, uint64_t tss_base, uint32_t tss_limit) {
    uint64_t base = tss_base;
    
    // החלק הראשון (8 בתים רגילים של GDT)
    gdt_set_gate(num, (uint32_t)base, tss_limit, 0x89, 0x40); 
    
    // החלק השני (עוד 8 בתים שמכילים את שאר ה-Base ב-64 ביט)
    struct gdt_entry* extended = (struct gdt_entry*)&gdt[num + 1];
    extended->limit_low = (base >> 32) & 0xFFFF;
    extended->base_low = (base >> 48) & 0xFFFF;
    extended->base_middle = 0;
    extended->access = 0;
    extended->granularity = 0;
    extended->base_high = 0;
}


void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}



void gdt_init() {
    gdt_set_gate(0, 0, 0, 0, 0);                // 0: Null
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // 1: Kernel Code (0x08)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // 2: Kernel Data (0x10)
    
    // הסדר כאן קריטי ל-sysret!
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0xCF); // 3: User Data (0x18 | 3 = 0x1B)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xAF); // 4: User Code (0x20 | 3 = 0x23)

    // TSS עובר לאינדקס 5
    memset(&g_tss, 0, sizeof(g_tss));

    extern char stack_top; // מוגדר ב-linker script

    g_tss.rsp0 = (uint64_t)&stack_top + KERNEL_VIRT_BASE;
    write_tss(5, (uint64_t)&g_tss, sizeof(g_tss) - 1);

    g_gdtr.limit = (sizeof(struct gdt_entry) * 7) - 1;
    g_gdtr.base  = (uint64_t)&gdt;
    __asm__ volatile("lgdt %0" : : "m"(g_gdtr));
    
    // טעינת ה-TSS מאינדקס 5 (5 * 8 = 0x28)
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
    
    gdt_reload_segments(); 
}