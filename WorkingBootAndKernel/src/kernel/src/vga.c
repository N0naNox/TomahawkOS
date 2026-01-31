/*
 * vga.c - Minimal VGA driver using framebuffer
 * - Writes characters to GOP framebuffer
 * - Simple cursor, newline handling, and color support
 */

#include "include/vga.h"
#include "include/font_8x16.h"
#include <stdint.h>
#include <stddef.h>

/* Framebuffer info (set by vga_init_fb) */
static volatile uint32_t* fb_buffer = 0;
static uint32_t fb_width = 1024;
static uint32_t fb_height = 768;
static uint32_t fb_pitch = 1024;

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_bg = VGA_COLOR_BLACK;
static uint8_t vga_fg = VGA_COLOR_LIGHT_GREY;

void vga_init_fb(void* framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
	fb_buffer = (volatile uint32_t*)framebuffer;
	fb_width = width;
	fb_height = height;
	fb_pitch = pitch;
}

static inline uint16_t vga_entry(char c, uint8_t bg, uint8_t fg) {
	uint16_t color = ((bg & 0x0F) << 4) | (fg & 0x0F);
	return (uint16_t)c | (uint16_t)color << 8;
}

void vga_set_color(uint8_t bg, uint8_t fg) {
	vga_bg = bg & 0x0F;
	vga_fg = fg & 0x0F;
}

void vga_clear(uint8_t bg, uint8_t fg) {
	(void)bg; (void)fg;
	if (!fb_buffer) return;
	/* Clear framebuffer to black */
	for (uint32_t i = 0; i < fb_height * fb_pitch; i++) {
		fb_buffer[i] = 0x00000000;
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
	if (!fb_buffer) return;
	/* Scroll framebuffer up by 16 pixels (one char height) */
	for (uint32_t y = 16; y < fb_height; y++) {
		for (uint32_t x = 0; x < fb_width; x++) {
			fb_buffer[(y - 16) * fb_pitch + x] = fb_buffer[y * fb_pitch + x];
		}
	}
	/* Clear last 16 rows */
	for (uint32_t y = fb_height - 16; y < fb_height; y++) {
		for (uint32_t x = 0; x < fb_width; x++) {
			fb_buffer[y * fb_pitch + x] = 0x00000000;
		}
	}
	if (vga_row > 0) vga_row--;
}

void vga_putc(char c) {
	if (!fb_buffer) return;
	
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
		/* Draw character using font */
		uint32_t x = vga_col * 9;  /* 8 pixels + 1 spacing */
		uint32_t y = vga_row * 16; /* 16 pixels tall */
		draw_char_8x16(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
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

