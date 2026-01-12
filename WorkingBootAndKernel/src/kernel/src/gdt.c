#include "include/gdt.h"

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
    // 0. Null Descriptor (חובה)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1. Kernel Code: Access 0x9A (Present, Ring 0, Exec, Readable)
    // Granularity 0xAF: 64-bit mode flag (L bit)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);

    // 2. Kernel Data: Access 0x92 (Present, Ring 0, Read/Write)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 3. User Code: Access 0xFA (Present, Ring 3, Exec, Readable)
    // שימו לב ל-0xFA - ה-A אומר Ring 3
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF);

    // 4. User Data: Access 0xF2 (Present, Ring 3, Read/Write)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);


    
    memset(&g_tss, 0, sizeof(g_tss));
    g_tss.iopb_offset = sizeof(g_tss);

    // נגדיר את ה-RSP0 ל-Stack הנוכחי של הקרנל (זמנית)
    extern char stack_top; // ה-Stack שמוגדר ב-linker script
    g_tss.rsp0 = (uint64_t)&stack_top + KERNEL_VIRT_BASE;

    write_tss(5, (uint64_t)&g_tss, sizeof(g_tss) - 1);


    // 5+6. TSS (כאן זה מורכב יותר כי TSS ב-64 ביט תופס 16 בתים)
    // נכון לעכשיו, בוא נטען את ה-GDT בלי TSS רק כדי לראות שהמערכת יציבה
    
    g_gdtr.limit = (sizeof(struct gdt_entry) * 7) - 1;
    g_gdtr.base  = (uint64_t)&gdt;

    // טעינת ה-GDT החדש
    __asm__ volatile("lgdt %0" : : "m"(g_gdtr));



    // 8. פקודת הקסם: טעינת ה-TSS לתוך המעבד
    // 0x28 הוא ה-Selector של ה-TSS (אינדקס 5 * 8 בתים)
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
    
    // ב-64 ביט צריך לעשות "Far Jump" או Reload ל-CS כדי שהשינוי יתפוס
    gdt_reload_segments(); 
}