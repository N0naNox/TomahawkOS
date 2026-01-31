/**
 * @file string.c
 * @author ajxs
 * @date Aug 2019
 * @brief String functionality.
 * Contains implementation for string functions.
 */

#include <string.h>
#include <stdint.h>

/**
 * strlen
 */
size_t strlen(const char* str)
{
	/** The length of the string to be output. */
	size_t len = 0;
	while(str[len]) {
		len++;
	}

	return len;
}


/*
 * memcpy
 * Copies n bytes from src to dst.
 * Returns dst.
 */
void* memcpy(void* dst, const void* src, size_t n)
{
	uint8_t* d = (uint8_t*)dst;
	const uint8_t* s = (const uint8_t*)src;

	for (size_t i = 0; i < n; i++)
	{
		d[i] = s[i];
	}

	return dst;
}


/*
 * memset
 * Sets n bytes at dst to value c.
 * Returns dst.
 */
void* memset(void* dst, int c, size_t n)
{
	uint8_t* d = (uint8_t*)dst;

	for (size_t i = 0; i < n; i++)
	{
		d[i] = (uint8_t)c;
	}

	return dst;
}