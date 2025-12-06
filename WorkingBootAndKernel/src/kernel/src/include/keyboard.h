/* keyboard.h - PS/2 keyboard driver (simple) */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Initialize keyboard driver and register IRQ handler */
void keyboard_init(void);

/* Non-blocking getchar (returns 0 if no char available) */
char keyboard_getchar(void);

#endif /* KEYBOARD_H */
