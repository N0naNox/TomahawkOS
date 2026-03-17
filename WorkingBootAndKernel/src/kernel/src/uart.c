/**
 * @file uart.c
 * @author ajxs
 * @date Aug 2019
 * @brief UART functionality.
 * Contains the implementation of the UART.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "include/hal_port_io.h"
#include <string.h>
#include <uart.h>

/* COM1 Port address*/
#define UART_PORT_COM1 0x3f8


static char hex_digit(uint8_t nib)
{
    return (nib < 10) ? ('0' + nib) : ('A' + (nib - 10));
}


/**
 * uart_initialize
 */
void uart_initialize(void)
{
	hal_outb(UART_PORT_COM1 + 1, 0x00);    // Disable all interrupts
	hal_outb(UART_PORT_COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
	hal_outb(UART_PORT_COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
	hal_outb(UART_PORT_COM1 + 1, 0x00);    //                  (hi byte)
	hal_outb(UART_PORT_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
	hal_outb(UART_PORT_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
	hal_outb(UART_PORT_COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}


/**
 * uart_is_recieve_buffer_empty
 */
bool uart_is_recieve_buffer_empty(void)
{
	return hal_inb(UART_PORT_COM1 + 5) & 1;
}


/**
 * uart_getchar
 */
char uart_getchar(void)
{
	while(!uart_is_recieve_buffer_empty());

	return hal_inb(UART_PORT_COM1);
}


/**
 * uart_is_transmit_buffer_empty
 */
bool uart_is_transmit_buffer_empty(void)
{
	return (hal_inb(UART_PORT_COM1 + 5) & 0x20) != 0;
}


/**
 * uart_putchar
 */
void uart_putchar(char a)
{
	while(!uart_is_transmit_buffer_empty());

	hal_outb(UART_PORT_COM1, a);
}


/**
 * uart_puts
 */
void uart_puts(const char* str)
{
	/** The length of the string being written to serial out. */
	size_t str_len = strlen(str);
	for(size_t i = 0; i < str_len; i++) {
		uart_putchar(str[i]);
	}
}


void uart_puthex(uint64_t val)
{
	uart_puts("0x");

	bool started = false;
	for (int i = 15; i >= 0; i--) {
		uint8_t nib = (val >> (i * 4)) & 0xF;

		if (nib != 0 || started || i == 0) {
			uart_putchar(hex_digit(nib));
			started = true;
		}

	}
}

void uart_putu(uint64_t value)
{
    char buffer[21];  // enough for 64-bit unsigned int (max 20 digits)
    int pos = 0;

    if (value == 0) {
        uart_putchar('0');
        return;
    }

    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }

    // Print digits in reverse
    for (int i = pos - 1; i >= 0; i--) {
        uart_putchar(buffer[i]);
    }
}
