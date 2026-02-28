// kernel.c - tiny kernel that uses arguments; no absolute addresses
#include <stdint.h>
#include <stddef.h>

__attribute__((section(".text.startup")))
void _start(void* magic_ptr, void* fb_ptr, size_t fb_pitch)
{
    volatile uint64_t* magic = (volatile uint64_t*)magic_ptr;
    if (magic) *magic = 0xDEADBEEFDEADBEEF;

    // very small framebuffer write (assumes 32bpp)
    if (fb_ptr && fb_pitch) {
        volatile uint32_t* p = (volatile uint32_t*)fb_ptr;
        p[0] = 0x00FF00; // some visible pixel (format may vary)
    }

    // halt
    for (;;) __asm__ volatile("hlt");
}
