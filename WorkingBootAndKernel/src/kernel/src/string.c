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

/*
 * strcmp
 * Compares two strings.
 * Returns 0 if equal, <0 if s1 < s2, >0 if s1 > s2.
 */
int strcmp(const char* s1, const char* s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/*
 * int_to_str
 * Converts an integer to a string in the given base.
 * Returns the buffer pointer.
 */
char* int_to_str(int value, char* buf, int base)
{
	char* p = buf;
	char* p1, *p2;
	unsigned int uvalue;
	int negative = 0;
	
	/* Handle negative numbers for base 10 */
	if (value < 0 && base == 10) {
		negative = 1;
		uvalue = (unsigned int)(-value);
	} else {
		uvalue = (unsigned int)value;
	}
	
	/* Generate digits in reverse */
	do {
		unsigned int digit = uvalue % base;
		*p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
		uvalue /= base;
	} while (uvalue > 0);
	
	/* Add negative sign */
	if (negative) {
		*p++ = '-';
	}
	
	/* Null terminate */
	*p = '\0';
	
	/* Reverse the string */
	p1 = buf;
	p2 = p - 1;
	while (p1 < p2) {
		char tmp = *p1;
		*p1++ = *p2;
		*p2-- = tmp;
	}
	
	return buf;
}