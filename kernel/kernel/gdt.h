#pragma once
#include <stdint.h>

#pragma pack(push, 1)

/* GDT entry structure (8 bytes) */
typedef struct GDTEntry 
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} GDTEntry;

/* GDTR structure (10 bytes) */
typedef struct GDTPtr 
{
    uint16_t limit;
    uint64_t base;
} GDTPtr;

#pragma pack(pop)

/* Function implemented in gdt.asm */
void gdt_load(GDTPtr* gdt_ptr);