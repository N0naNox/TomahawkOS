#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Define segment selectors (offsets into the GDT)
// gdt.h
#define KERN_CODE_SEL 0x08
#define KERN_DATA_SEL 0x10
#define USER_DATA_SEL 0x18  // אינדקס 3 - חייב להיות לפני הקוד עבור sysret
#define USER_CODE_SEL 0x20  // אינדקס 4
#define TSS_SEL       0x28  // אינדקס 5
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL


struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;      // ה-Stack שהמעבד יטען במעבר ל-Ring 0
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];    // Interrupt Stack Table (אופציונלי)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));


void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void gdt_reload_segments(void);

/* Jump to user mode (Ring 3) - implemented in gdt_asm.asm */
void jump_to_user(uint64_t entry_point, uint64_t user_stack);

#endif