/**
 * @file string.h
 * @author ajxs
 * @date Aug 2019
 * @brief String functionality.
 * Contains definitions for string functions.
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>

/**
 * @brief Gets the size of a string.
 * Returns the number of chars making up a constant string.
 * @param str[in]   The string to get the length of.
 * @return          The size of the string.
 */
size_t strlen(const char* str);

/**
 * @brief Copy memory.
 * Copies n bytes from src to dst.
 * @param dst[out]  Destination buffer.
 * @param src[in]   Source buffer.
 * @param n[in]     Number of bytes to copy.
 * @return          Pointer to dst.
 */
void* memcpy(void* dst, const void* src, size_t n);

/**
 * @brief Set memory.
 * Sets n bytes at dst to value c.
 * @param dst[out]  Destination buffer.
 * @param c[in]     Value to set (as int).
 * @param n[in]     Number of bytes to set.
 * @return          Pointer to dst.
 */
void* memset(void* dst, int c, size_t n);

int strncmp(const char* s1, const char* s2, size_t n);

char* strncpy(char* dest, const char* src, size_t n);

#endif
