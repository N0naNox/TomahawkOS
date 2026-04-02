/**
 * @file checksum.h
 * @brief Network Checksum Utilities
 *
 * Provides the standard Internet one's-complement checksum used by
 * IPv4 headers, ICMP, UDP, and TCP.
 *
 * Algorithm (RFC 1071):
 *   1. Sum all 16-bit words of the data.
 *   2. Fold 32-bit carry into the low 16 bits.
 *   3. Take the one's complement of the result.
 */

#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Compute the Internet checksum over `len` bytes.
 *
 * Can be called with `initial = 0` for a fresh checksum,
 * or with a previous partial sum to incrementally checksum
 * multiple non-contiguous regions (e.g. UDP pseudo-header + body).
 *
 * @param data     Pointer to the data.
 * @param len      Number of bytes.
 * @param initial  Partial sum from a previous call (0 to start fresh).
 * @return         The 16-bit one's-complement checksum.
 */
static inline uint16_t net_checksum(const void *data, size_t len,
                                    uint32_t initial)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = initial;

    /* Sum 16-bit words */
    while (len > 1) {
        sum += (uint16_t)(p[0] | (p[1] << 8));   /* little-endian read */
        p   += 2;
        len -= 2;
    }

    /* Odd trailing byte */
    if (len == 1) {
        sum += p[0];
    }

    /* Fold 32-bit carries into 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/**
 * @brief Verify a received checksum.
 *
 * The checksum of a block that includes a correct checksum field
 * should produce 0x0000 (or 0xFFFF before complement).
 *
 * @return Non-zero if valid, zero if corrupt.
 */
static inline int net_checksum_verify(const void *data, size_t len)
{
    /* Compute checksum over the whole block including the checksum field.
       If correct, the result is 0 (one's complement of 0xFFFF = 0x0000). */
    return net_checksum(data, len, 0) == 0;
}

#endif /* CHECKSUM_H */
