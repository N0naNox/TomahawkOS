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

/**
 * @brief Compare two strings.
 * @param s1[in]    First string.
 * @param s2[in]    Second string.
 * @return          0 if equal, <0 if s1 < s2, >0 if s1 > s2.
 */
int strcmp(const char* s1, const char* s2);

/**
 * @brief Convert integer to string.
 * @param value[in] Integer to convert.
 * @param buf[out]  Buffer to write to.
 * @param base[in]  Numeric base (10 for decimal, 16 for hex).
 * @return          Pointer to buf.
 */
char* int_to_str(int value, char* buf, int base);

/**
 * @brief Compare at most n bytes of two strings.
 * @param s1[in]  First string.
 * @param s2[in]  Second string.
 * @param n[in]   Maximum number of bytes to compare.
 * @return        0 if equal, <0 if s1 < s2, >0 if s1 > s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Copy at most n bytes from src to dst, NUL-padding.
 * @param dst[out]  Destination buffer.
 * @param src[in]   Source string.
 * @param n[in]     Maximum bytes to copy.
 * @return          Pointer to dst.
 */
char *strncpy(char *dst, const char *src, size_t n);

#endif
