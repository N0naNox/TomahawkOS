/*
 * hal_port_io.c
 * C wrappers for low-level NASM port I/O primitives.
 * Keeps a clean C interface for kernel code while the actual
 * in/out instructions live in a small NASM file.
 */

#include "include/hal_port_io.h"

/* Call the assembly primitives. These are tiny and follow the SysV ABI:
 * - first argument in %rdi (port)
 * - second argument in %rsi (value)
 * returns in %rax (zero-extended)
 */

uint8_t hal_inb(uint16_t port) {
    return hal_inb_asm(port);
}

void hal_outb(uint16_t port, uint8_t value) {
    hal_outb_asm(port, value);
}

uint16_t hal_inw(uint16_t port) {
    return hal_inw_asm(port);
}

void hal_outw(uint16_t port, uint16_t value) {
    hal_outw_asm(port, value);
}

uint32_t hal_inl(uint16_t port) {
    return hal_inl_asm(port);
}

void hal_outl(uint16_t port, uint32_t value) {
    hal_outl_asm(port, value);
}
