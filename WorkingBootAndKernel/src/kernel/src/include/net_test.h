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

/**
 * @brief Run the socket-layer self-test over loopback.
 *
 * Exercises the socket API (sock_create, sock_bind, sock_sendto,
 * sock_recvfrom, sock_close) end-to-end over 127.0.0.1.
 * Because loopback is synchronous the received datagram is already
 * in the RX ring by the time sendto() returns, so the test never
 * blocks.
 *
 * Results are printed to serial (UART).
 */
void socket_self_test(void);

/**
 * @brief Run the network interface abstraction self-test.
 *
 * Exercises every function added to the NIC abstraction layer:
 *   net_device_find_by_ip, net_device_route,
 *   net_device_up / net_device_down (lifecycle + idempotency),
 *   net_device_transmit (raw send + stat accounting),
 *   net_device_poll_all (smoke-test, lo always returns -1),
 *   net_device_print_stats / net_device_print_all_stats.
 *
 * Results are printed to serial (UART).
 */
void net_device_iface_test(void);

#endif /* NET_TEST_H */
