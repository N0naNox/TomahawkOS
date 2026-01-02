#include "gdt.h"

/* 64-bit GDT entry */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* GDT layout:
 * 0x00 null
 * 0x08 kernel code
 * 0x10 kernel data
 * 0x18 user data
 * 0x20 user code
 */
static struct gdt_entry gdt[5];
static struct gdt_ptr gp;

extern void gdt_flush(struct gdt_ptr*);

/* helper */
static void gdt_set(int i, uint8_t access) {
    gdt[i] = (struct gdt_entry){
        .limit_low = 0,
        .base_low  = 0,
        .base_mid  = 0,
        .access    = access,
        .gran      = 0x20,   // long mode
        .base_high = 0
    };
}

void gdt_init(void) {
    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)&gdt;

    gdt_set(0, 0x00); /* null */
    gdt_set(1, 0x9A); /* kernel code */
    gdt_set(2, 0x92); /* kernel data */
    gdt_set(3, 0xF2); /* user data */
    gdt_set(4, 0xFA); /* user code */

    gdt_flush(&gp);
}
