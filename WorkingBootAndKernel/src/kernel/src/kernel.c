/**
 * @file kernel.c
 * @author ajxs
 * @date Aug 2019
 * @brief Kernel entry.
 * Contains the kernel entry point.
 */

#include <stdint.h>
#include <stddef.h>
#include "include/vga.h"
#include <boot.h>

/**
 * @brief The kernel main program.
 * This is the kernel main entry point and its main program.
 */
void kernel_main(Boot_Info* boot_info);

/* Port I/O functions for VGA mode setting */
static inline void outb(uint16_t port, uint8_t value) {
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

/* Simple 5x7 bitmap font for basic characters */
static const uint8_t font_5x7[][7] = {
	/* 'K' */ {0x44, 0x4A, 0x52, 0x62, 0x52, 0x4A, 0x44},
	/* 'E' */ {0x7E, 0x82, 0x80, 0x7E, 0x80, 0x82, 0x7E},
	/* 'R' */ {0x7E, 0x82, 0x82, 0x7E, 0x88, 0x84, 0x82},
	/* 'N' */ {0x82, 0x86, 0x8A, 0x92, 0xA2, 0xC2, 0x82},
	/* 'L' */ {0x80, 0x80, 0x80, 0x80, 0x80, 0x82, 0x7E},
	/* 'U' */ {0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x7C},
	/* 'G' */ {0x7E, 0x82, 0x80, 0x9E, 0x82, 0x82, 0x7E},
	/* ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	/* 'R' */ {0x7E, 0x82, 0x82, 0x7E, 0x88, 0x84, 0x82},
	/* 'I' */ {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E},
};

/**
 * @brief Draw a character from the font
 */
void draw_char(volatile uint32_t* fb, uint32_t pitch, char c, uint32_t x, uint32_t y) {
	int idx = -1;
	switch (c) {
		case 'K': idx = 0; break;
		case 'E': idx = 1; break;
		case 'R': idx = 2; break;
		case 'N': idx = 3; break;
		case 'L': idx = 4; break;
		case 'U': idx = 5; break;
		case 'G': idx = 6; break;
		case ' ': idx = 7; break;
		case 'I': idx = 9; break;
		default: return;
	}
	
	const uint8_t* bitmap = font_5x7[idx];
	
	/* Draw each row of the character (7 rows) */
	for (int row = 0; row < 7; row++) {
		uint8_t bits = bitmap[row];
		/* Draw each column (8 pixels wide, bit 7 to 0) */
		for (int col = 0; col < 8; col++) {
			if (bits & (0x80 >> col)) {
				uint32_t px = x + col;
				uint32_t py = y + row;
				fb[py * pitch + px] = 0xFFFFFFFF;  /* White */
			}
		}
	}
}

void kernel_main(Boot_Info* boot_info)
{
	/* Send banner to serial */
	const char* banner = "\n=== KERNEL RUNNING ===\n";
	for (int i = 0; banner[i]; i++) {
		outb(0x3F8, banner[i]);
	}
	
	/* Check if we have framebuffer */
	if (boot_info->video_mode_info.framebuffer_pointer == NULL) {
		const char* no_gfx = "ERROR: No framebuffer\n";
		for (int i = 0; no_gfx[i]; i++) {
			outb(0x3F8, no_gfx[i]);
		}
		while (1) {
			__asm__ volatile("pause");
		}
	}
	
	/* Get framebuffer info */
	volatile uint32_t* fb = (uint32_t*)boot_info->video_mode_info.framebuffer_pointer;
	uint32_t width = boot_info->video_mode_info.horizontal_resolution;
	uint32_t height = boot_info->video_mode_info.vertical_resolution;
	uint32_t pitch = boot_info->video_mode_info.pixels_per_scaline;
	
	/* Clear screen to black */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			fb[y * pitch + x] = 0x00000000;  /* Black */
		}
	}
	
	/* Draw "KERNEL RUNNING" with bitmap font */
	const char* text = "KERNEL RUNNING";
	uint32_t start_x = 100;
	uint32_t start_y = 100;
	uint32_t char_width = 10;  /* 8 pixels + 2 spacing */
	
	for (int i = 0; text[i]; i++) {
		draw_char(fb, pitch, text[i], start_x + (i * char_width), start_y);
	}
	
	const char* done = "Framebuffer written\n";
	for (int i = 0; done[i]; i++) {
		outb(0x3F8, done[i]);
	}

	/* Infinite loop */
	while (1) {
		__asm__ volatile("pause");
	}
}
