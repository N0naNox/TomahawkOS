#include "gdt.h"

extern void cpu_halt(void);

void kernel_print(const char* str) 
{
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    uint16_t color = 0x0F00;  // White on black

    while (*str) {
        *vga++ = color | *str++;
    }
}

void kernel_main()
{
    kernel_print("Kernel is running!");
    cpu_halt();
}