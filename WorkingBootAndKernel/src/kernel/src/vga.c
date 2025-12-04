/*
 * vga.c - Minimal VGA text-mode driver
 * - Writes characters to 0xB8000 in text mode
 * - Simple cursor, newline handling, and color support
 */

#include "include/vga.h"
#include <stdint.h>
#include <stddef.h>

/* VGA text buffer is at physical 0xB8000 */
static volatile uint16_t* const VGA_BUFFER = (uint16_t*)0xB8000;

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_bg = VGA_COLOR_BLACK;
static uint8_t vga_fg = VGA_COLOR_LIGHT_GREY;

static inline uint16_t vga_entry(char c, uint8_t bg, uint8_t fg) {
	uint16_t color = ((bg & 0x0F) << 4) | (fg & 0x0F);
	return (uint16_t)c | (uint16_t)color << 8;
}

void vga_set_color(uint8_t bg, uint8_t fg) {
	vga_bg = bg & 0x0F;
	vga_fg = fg & 0x0F;
}

void vga_clear(uint8_t bg, uint8_t fg) {
	uint16_t entry = vga_entry(' ', bg, fg);
	for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
		VGA_BUFFER[i] = entry;
	}
	vga_row = 0;
	vga_col = 0;
}

void vga_init(void) {
	vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	vga_clear(vga_bg, vga_fg);
}

void vga_set_cursor(int row, int col) {
	if (row < 0) row = 0;
	if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
	if (col < 0) col = 0;
	if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
	vga_row = row;
	vga_col = col;
}

void vga_get_cursor(int* row, int* col) {
	if (row) *row = vga_row;
	if (col) *col = vga_col;
}

/* Scroll the screen up by one line */
static void vga_scroll(void) {
	for (int r = 1; r < VGA_HEIGHT; ++r) {
		for (int c = 0; c < VGA_WIDTH; ++c) {
			VGA_BUFFER[(r - 1) * VGA_WIDTH + c] = VGA_BUFFER[r * VGA_WIDTH + c];
		}
	}
	/* clear last line */
	uint16_t blank = vga_entry(' ', vga_bg, vga_fg);
	for (int c = 0; c < VGA_WIDTH; ++c) {
		VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = blank;
	}
	if (vga_row > 0) vga_row--;
}

void vga_putc(char c) {
	if (c == '\n') {
		vga_col = 0;
		++vga_row;
	} else if (c == '\r') {
		vga_col = 0;
	} else if (c == '\t') {
		vga_col = (vga_col + 8) & ~(8 - 1);
		if (vga_col >= VGA_WIDTH) {
			vga_col = 0;
			++vga_row;
		}
	} else {
		VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_bg, vga_fg);
		++vga_col;
		if (vga_col >= VGA_WIDTH) {
			vga_col = 0;
			++vga_row;
		}
	}

	if (vga_row >= VGA_HEIGHT) {
		vga_scroll();
	}
}

void vga_write(const char* str) {
	for (const char* p = str; *p; ++p) vga_putc(*p);
}

