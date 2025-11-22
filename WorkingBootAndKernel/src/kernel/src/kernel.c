/**
 * @file kernel.c
 * @author ajxs
 * @date Aug 2019
 * @brief Kernel entry.
 * Contains the kernel entry point.
 */

#include <stdint.h>
#include <boot.h>
#include <uart.h>

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
	// Initialise the UART.
	uart_initialize();
	uart_puts("Kernel: LET'S GOOOOOOO RUNNING.\n");


	while(1);
}
