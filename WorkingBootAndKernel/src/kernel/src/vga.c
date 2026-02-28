/*
 * vga.c - Minimal VGA driver using GOP framebuffer
 * - Writes characters using scaled bitmap font
 * - Auto-selects font scale:
 *     scale 1 (8x16)  for resolutions up to 1024x768
 *     scale 2 (16x32) for 1080p / 1440p
 *     scale 3 (24x48) for 4K
 * - Dynamic text grid sizing
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

/* Font metrics — computed in vga_init_fb */
static uint32_t font_scale = 1;     /* 1, 2, or 3 */
static uint32_t char_w = 9;         /* pixel width of one cell  (8*scale + scale spacing) */
static uint32_t char_h = 16;        /* pixel height of one cell (16*scale) */

/* Dynamic text grid */
int vga_cols = 80;
int vga_rows = 48;

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_bg = VGA_COLOR_BLACK;
static uint8_t vga_fg = VGA_COLOR_LIGHT_GREY;

void vga_init_fb(void* framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
	fb_buffer = (volatile uint32_t*)framebuffer;
	fb_width = width;
	fb_height = height;
	fb_pitch = pitch;

	/* Auto-select font scale based on resolution */
	if (width >= 3840 && height >= 2160) {
		font_scale = 3;   /* 4K: native 24x48 glyphs */
	} else if (width >= 1920 && height >= 1080) {
		font_scale = 2;   /* 1080p/1440p: native 16x32 glyphs */
	} else {
		font_scale = 1;   /* 1024x768 or smaller: 8x16 */
	}

	/* Character cell size — native font dimensions + 1-col spacing */
	if (font_scale >= 3) {
		char_w = 24 + 3;   /* 27 pixels */
		char_h = 48;
	} else if (font_scale == 2) {
		char_w = 16 + 2;   /* 18 pixels */
		char_h = 32;
	} else {
		char_w = 8 + 1;    /* 9 pixels */
		char_h = 16;
	}

	/* Compute text grid dimensions */
	vga_cols = (int)(fb_width / char_w);
	vga_rows = (int)(fb_height / char_h);
	if (vga_cols < 1) vga_cols = 1;
	if (vga_rows < 1) vga_rows = 1;
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
	if (row >= vga_rows) row = vga_rows - 1;
	if (col < 0) col = 0;
	if (col >= vga_cols) col = vga_cols - 1;
	vga_row = row;
	vga_col = col;
}

void vga_get_cursor(int* row, int* col) {
	if (row) *row = vga_row;
	if (col) *col = vga_col;
}

uint32_t vga_get_font_scale(void) {
	return font_scale;
}

void vga_get_dimensions(int* cols, int* rows) {
	if (cols) *cols = vga_cols;
	if (rows) *rows = vga_rows;
}

/* Scroll the screen up by one line (char_h pixels) */
static void vga_scroll(void) {
	if (!fb_buffer) return;
	/* Copy rows up by char_h pixels */
	for (uint32_t y = char_h; y < fb_height; y++) {
		for (uint32_t x = 0; x < fb_width; x++) {
			fb_buffer[(y - char_h) * fb_pitch + x] = fb_buffer[y * fb_pitch + x];
		}
	}
	/* Clear the last char_h rows */
	uint32_t clear_start = fb_height > char_h ? fb_height - char_h : 0;
	for (uint32_t y = clear_start; y < fb_height; y++) {
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
		if (vga_col >= vga_cols) {
			vga_col = 0;
			++vga_row;
		}
	} else {
		/* Draw character using native-resolution font */
		uint32_t x = vga_col * char_w;
		uint32_t y = vga_row * char_h;
		if (font_scale >= 3) {
			draw_char_24x48(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
		} else if (font_scale == 2) {
			draw_char_16x32(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
		} else {
			draw_char_8x16(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
		}
		++vga_col;
		if (vga_col >= vga_cols) {
			vga_col = 0;
			++vga_row;
		}
	}

	if (vga_row >= vga_rows) {
		vga_scroll();
	}
}

void vga_write(const char* str) {
	for (const char* p = str; *p; ++p) vga_putc(*p);
}

void vga_draw_char_at(int row, int col, char c) {
	if (!fb_buffer) return;
	uint32_t x = (uint32_t)col * char_w;
	uint32_t y = (uint32_t)row * char_h;
	if (font_scale >= 3) {
		draw_char_24x48(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
	} else if (font_scale == 2) {
		draw_char_16x32(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
	} else {
		draw_char_8x16(fb_buffer, fb_pitch, c, x, y, fb_width, fb_height);
	}
}

void vga_clear_char(int row, int col) {
	if (!fb_buffer) return;
	uint32_t x = (uint32_t)col * char_w;
	uint32_t y = (uint32_t)row * char_h;
	for (uint32_t py = y; py < y + char_h && py < fb_height; py++) {
		for (uint32_t px = x; px < x + char_w && px < fb_width; px++) {
			fb_buffer[py * fb_pitch + px] = 0x00000000;
		}
	}
}

void vga_draw_cursor(void) {
	if (!fb_buffer) return;
	/* Draw a 2-pixel-high underline at the bottom of the current cell */
	uint32_t x = (uint32_t)vga_col * char_w;
	uint32_t y = (uint32_t)vga_row * char_h + char_h - 2;
	for (uint32_t py = y; py < y + 2 && py < fb_height; py++) {
		for (uint32_t px = x; px < x + char_w && px < fb_width; px++) {
			fb_buffer[py * fb_pitch + px] = 0x00AAAAAA; /* light grey */
		}
	}
}

void vga_erase_cursor(void) {
	if (!fb_buffer) return;
	/* Erase the 2-pixel-high underline at the bottom of the current cell */
	uint32_t x = (uint32_t)vga_col * char_w;
	uint32_t y = (uint32_t)vga_row * char_h + char_h - 2;
	for (uint32_t py = y; py < y + 2 && py < fb_height; py++) {
		for (uint32_t px = x; px < x + char_w && px < fb_width; px++) {
			fb_buffer[py * fb_pitch + px] = 0x00000000;
		}
	}
}

