/*
 * hal_port_io.h
 * Minimal x86-64 HAL for raw I/O ports (in/out instructions)
 * - Provides safe, freestanding prototypes for port I/O
 * - Implementation is in C and NASM assembly (hal_port_io.asm)
 * - Follows System V AMD64 calling convention
 */

#ifndef HAL_PORT_IO_H
#define HAL_PORT_IO_H

#include <stdint.h>

/* 8-bit */
uint8_t hal_inb(uint16_t port);
void    hal_outb(uint16_t port, uint8_t value);

/* 16-bit */
uint16_t hal_inw(uint16_t port);
void     hal_outw(uint16_t port, uint16_t value);

/* 32-bit */
uint32_t hal_inl(uint16_t port);
void     hal_outl(uint16_t port, uint32_t value);

/* Assembly primitives (implemented in hal_port_io.asm)
 * The asm symbols have `_asm` suffix and are kept internal.
 */
extern uint8_t  hal_inb_asm(uint16_t port);
extern void     hal_outb_asm(uint16_t port, uint8_t value);
extern uint16_t hal_inw_asm(uint16_t port);
extern void     hal_outw_asm(uint16_t port, uint16_t value);
extern uint32_t hal_inl_asm(uint16_t port);
extern void     hal_outl_asm(uint16_t port, uint32_t value);

#endif /* HAL_PORT_IO_H */
