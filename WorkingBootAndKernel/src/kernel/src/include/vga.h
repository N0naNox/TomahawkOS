/*
 * vga.h - Minimal VGA text-mode driver API
 * - simple functions for boot/kernel text output
 * - writes to GOP framebuffer with scalable font
 * - no dynamic allocation, freestanding-friendly
 */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/*
 * VGA text dimensions are now computed at runtime based on actual
 * framebuffer resolution and chosen font scale.
 * Use vga_get_dimensions() to query current cols/rows.
 */
extern int vga_cols;   /* number of text columns */
extern int vga_rows;   /* number of text rows    */

/* Legacy macros — redirect to the runtime variables */
#define VGA_WIDTH  vga_cols
#define VGA_HEIGHT vga_rows

/* VGA color constants */
enum vga_color {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

/* Initialize VGA driver (clears screen) */
void vga_init(void);

/* Initialize VGA with framebuffer info */
void vga_init_fb(void* framebuffer, uint32_t width, uint32_t height, uint32_t pitch);

/* Clear screen with color */
void vga_clear(uint8_t bg, uint8_t fg);

/* Put a single character at current cursor, handling newlines */
void vga_putc(char c);

/* Put a null-terminated string */
void vga_write(const char* str);

/* Move cursor to given row/col */
void vga_set_cursor(int row, int col);

/* Get current cursor row/col */
void vga_get_cursor(int* row, int* col);

/* Set current text colors (bg and fg are vga_color values) */
void vga_set_color(uint8_t bg, uint8_t fg);

/* Get current font scale factor (1, 2, or 3) */
uint32_t vga_get_font_scale(void);

/* Get text grid dimensions */
void vga_get_dimensions(int* cols, int* rows);

/* Draw a single character at grid position (row, col) */
void vga_draw_char_at(int row, int col, char c);

/* Clear (blank) one character cell at grid position (row, col) */
void vga_clear_char(int row, int col);

/* Draw visible underline cursor at the current cursor position */
void vga_draw_cursor(void);

/* Erase visible underline cursor at the current cursor position */
void vga_erase_cursor(void);

#endif /* VGA_H */

