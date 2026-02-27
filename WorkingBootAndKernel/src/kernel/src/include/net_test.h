/**
 * @file net_test.h
 * @brief Network stack self-test (loopback)
 */

#ifndef NET_TEST_H
#define NET_TEST_H

/**
 * @brief Run the loopback self-test suite.
 *
 * Exercises the full network stack over the "lo" interface:
 *   1. ICMP Echo Request to 127.0.0.1  → expects Echo Reply
 *   2. UDP send to 127.0.0.1           → expects callback delivery
 *
 * Results are printed to serial (UART).
 */
void net_test_loopback(void);

#endif /* NET_TEST_H */
