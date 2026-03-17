/* keyboard.h - PS/2 keyboard driver (simple) */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Initialize keyboard driver and register IRQ handler */
void keyboard_init(void);

/* Non-blocking getchar (returns 0 if no char available) */
char keyboard_getchar(void);
void keyboard_poll_once(void);

/* Reset keyboard buffer - use when buffer may be corrupted */
void keyboard_reset_buffer(void);

/* Shutdown the system (ACPI/QEMU debug exit) */
void shutdown_system(void);

#endif /* KEYBOARD_H */
