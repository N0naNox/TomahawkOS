/**
 * @file kernel.c
 * @author ajxs
 * @date Aug 2019
 * @brief Kernel entry.
 * Contains the kernel entry point.
 */

#include <stdint.h>
#include "include/vga.h"
#include "include/idt.h"
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





/**
 * kernel_main
 */
void kernel_main(Boot_Info* boot_info)
{
	/* Initialize VGA first so we have immediate on-screen output */
	vga_init();
	vga_write("Kernel: VGA initialized.\n");

	/* Initialize UART for serial logging */
	uart_initialize();
	uart_puts("Kernel: UART initialized.\n");

	/* Install IDT (interrupts) */
	idt_install();
	
	vga_write("Kernel: IDT installed.\n");
	uart_puts("Kernel: IDT installed.\n");

	/* Main idle loop - use HLT to save CPU until interrupts occur */
	for (;;) {
		__asm__ volatile("hlt");
	}
}
