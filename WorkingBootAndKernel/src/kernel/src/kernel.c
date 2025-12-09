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
#include "include/font_8x16.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include <boot.h>
#include <uart.h>
#include "timer.h"

/** Whether to draw a test pattern to video output. */
#define DRAW_TEST_SCREEN 1

#define TEST_SCREEN_COL_NUM             4
#define TEST_SCREEN_ROW_NUM             3
#define TEST_SCREEN_TOTAL_TILES         TEST_SCREEN_COL_NUM * TEST_SCREEN_ROW_NUM
#define TEST_SCREEN_PRIMARY_COLOUR      0x00FF40FF
#define TEST_SCREEN_SECONDARY_COLOUR    0x00FF00CF


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

/**
 * @brief Write text to the framebuffer
 */
void write_text(volatile uint32_t* fb, uint32_t pitch, uint32_t width, uint32_t height,
                const char* text, uint32_t x, uint32_t y)
{
	for (int i = 0; text[i]; i++) {
		draw_char_8x16(fb, pitch, text[i], x + (i * 9), y, width, height);
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
	
	/* Draw "KERNEL RUNNING" with 8x16 bitmap font */
	const char* text = "KERNEL RUNNING";
	uint32_t start_x = 100;
	uint32_t start_y = 100;
	
	write_text(fb, pitch, width, height, text, start_x, start_y);
	
	const char* done = "Framebuffer written\n";
	for (int i = 0; done[i]; i++) {
		outb(0x3F8, done[i]);
	}

	/* Initialize VGA with framebuffer */
	vga_init_fb((void*)fb, width, height, pitch);
	vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	
	/* Initialize IDT and keyboard */
	idt_install();
	keyboard_init();
	
	/* Enable interrupts */
	__asm__ volatile("sti");
	
	const char* ready = "Keyboard ready - type something!\n";
	for (int i = 0; ready[i]; i++) {
		outb(0x3F8, ready[i]);
	}
	
	vga_write("Type on keyboard: ");

	/* Infinite loop */
	while (1) {
		__asm__ volatile("pause");
	}
}
