/**
 * @file net_test.c
 * @brief Network stack self-test — prove the loopback interface works
 *
 * Runs two tests over the "lo" (127.0.0.1) device and prints
 * pass/fail verdicts to serial (UART).
 *
 * Test 1 — ICMP ping loopback
 *   Sends an ICMP Echo Request to 127.0.0.1.
 *   The stack should:
 *     a) transmit via lo → loopback copies frame → ethernet_receive()
 *     b) IPv4 demux → icmp_receive() sees Echo Request
 *     c) icmp_receive() builds Echo Reply → sends back through lo
 *     d) IPv4 demux → icmp_receive() logs "echo reply received"
 *   We verify by checking that lo's TX/RX counters advanced.
 *
 * Test 2 — UDP loopback
 *   Binds a callback on UDP port 7777, then sends a 5-byte payload
 *   ("HELLO") to 127.0.0.1:7777.
 *   The callback sets a flag and compares the received data.
 *   We check the flag to determine pass/fail.
 */

#include "include/net_test.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/loopback.h"
#include "include/icmp.h"
#include "include/udp.h"
#include "include/socket.h"
#include <uart.h>

/* ====================================================================
 *  Shared state for the UDP callback test
 * ==================================================================== */

static volatile int udp_test_passed = 0;

/** Expected payload */
static const uint8_t udp_test_payload[] = { 'H', 'E', 'L', 'L', 'O' };
#define UDP_TEST_PORT  7777

/**
 * @brief UDP receive callback for test 2.
 *
 * Called by the UDP layer when a datagram arrives on port 7777.
 * Verifies the source IP (127.0.0.1), payload length, and content.
 */
static void udp_test_callback(struct net_device *dev,
                               ipv4_addr_t src_ip,
                               uint16_t src_port,
                               const uint8_t *data,
                               uint16_t data_len)
{
    (void)dev;
    (void)src_port;

    uart_puts("[net_test]   UDP callback invoked\n");

    /* Check source IP */
    if (!ipv4_equal(src_ip, IPV4(127, 0, 0, 1))) {
        uart_puts("[net_test]   FAIL: unexpected source IP\n");
        return;
    }

    /* Check payload length */
    if (data_len != sizeof(udp_test_payload)) {
        uart_puts("[net_test]   FAIL: unexpected payload length (got ");
        uart_putu(data_len);
        uart_puts(", expected ");
        uart_putu(sizeof(udp_test_payload));
        uart_puts(")\n");
        return;
    }

    /* Check payload content */
    for (uint16_t i = 0; i < data_len; i++) {
        if (data[i] != udp_test_payload[i]) {
            uart_puts("[net_test]   FAIL: payload mismatch at byte ");
            uart_putu(i);
            uart_puts("\n");
            return;
        }
    }

    uart_puts("[net_test]   UDP payload verified: \"HELLO\"\n");
    udp_test_passed = 1;
}

/* ====================================================================
 *  Test runner
 * ==================================================================== */

void net_test_loopback(void)
{
    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  NETWORK STACK SELF-TEST  (loopback 127.0.0.1)\n");
    uart_puts("=============================================================\n");

    net_device_t *lo = loopback_dev();
    if (!lo) {
        uart_puts("[net_test] ABORT: loopback device not initialised\n");
        return;
    }

    uart_puts("[net_test] lo device found: ");
    uart_puts(lo->name);
    uart_puts("  ip=127.0.0.1  link_up=");
    uart_putu(lo->link_up);
    uart_puts("\n\n");

    /* ================================================================
     *  Test 1:  ICMP Echo (ping 127.0.0.1)
     * ================================================================ */
    uart_puts("[net_test] --- Test 1: ICMP ping 127.0.0.1 ---\n");

    uint64_t tx_before = lo->tx_packets;
    uint64_t rx_before = lo->rx_packets;

    int ret = icmp_send_echo_request(lo,
                                     IPV4(127, 0, 0, 1),
                                     /*id=*/1,
                                     /*seq=*/1,
                                     "ABCD",   /* 4-byte payload */
                                     4);

    if (ret != 0) {
        uart_puts("[net_test]   FAIL: icmp_send_echo_request returned ");
        uart_putu((uint64_t)ret);
        uart_puts("\n");
    } else {
        uart_puts("[net_test]   icmp_send_echo_request returned 0 (OK)\n");
    }

    /* Because loopback is synchronous, by this point the echo request
     * has already been received, replied to, and the reply received.
     * Each direction = 1 Ethernet send + 1 Ethernet receive, but
     * the Ethernet layer counts on the same device.
     *
     * Expected deltas:
     *   tx_packets += 3  (request IP frame + reply IP frame + possibly ARP)
     *   rx_packets += 3  (same)
     *
     * We just check that counters advanced at all.
     */
    uint64_t tx_after = lo->tx_packets;
    uint64_t rx_after = lo->rx_packets;

    uart_puts("[net_test]   lo stats: tx=");
    uart_putu(tx_after);
    uart_puts(" (+");
    uart_putu(tx_after - tx_before);
    uart_puts(")  rx=");
    uart_putu(rx_after);
    uart_puts(" (+");
    uart_putu(rx_after - rx_before);
    uart_puts(")\n");

    if (tx_after > tx_before && rx_after > rx_before) {
        uart_puts("[net_test]   PASS: ICMP loopback ping succeeded\n");
    } else {
        uart_puts("[net_test]   FAIL: counters did not advance\n");
    }

    uart_puts("\n");

    /* ================================================================
     *  Test 2:  UDP send/receive on 127.0.0.1:7777
     * ================================================================ */
    uart_puts("[net_test] --- Test 2: UDP loopback (port 7777) ---\n");

    udp_test_passed = 0;

    /* Bind our test callback */
    ret = udp_bind(UDP_TEST_PORT, udp_test_callback);
    if (ret != 0) {
        uart_puts("[net_test]   FAIL: udp_bind returned ");
        uart_putu((uint64_t)ret);
        uart_puts("\n");
    } else {
        uart_puts("[net_test]   Bound UDP port 7777\n");
    }

    /* Send the test datagram */
    ret = udp_send(lo,
                   IPV4(127, 0, 0, 1),
                   /*src_port=*/12345,
                   /*dst_port=*/UDP_TEST_PORT,
                   udp_test_payload,
                   sizeof(udp_test_payload));

    if (ret != 0) {
        uart_puts("[net_test]   FAIL: udp_send returned ");
        uart_putu((uint64_t)ret);
        uart_puts("\n");
    } else {
        uart_puts("[net_test]   udp_send returned 0 (OK)\n");
    }

    /* The loopback is synchronous, so the callback should have fired */
    if (udp_test_passed) {
        uart_puts("[net_test]   PASS: UDP loopback succeeded\n");
    } else {
        uart_puts("[net_test]   FAIL: UDP callback was not invoked or data mismatch\n");
    }

    /* Clean up */
    udp_unbind(UDP_TEST_PORT);

    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  NET SELF-TEST COMPLETE\n");
    uart_puts("=============================================================\n\n");
}

/* ====================================================================
 *  socket_self_test — socket-layer end-to-end test over loopback
 * ==================================================================== */

/** Port used exclusively by this test — kept clear of net_test ports. */
#define SOCK_TEST_PORT  8888

/** Payload sent and expected back. */
static const uint8_t sock_test_payload[] = { 'S', 'O', 'C', 'K', 'E', 'T' };
#define SOCK_TEST_LEN  ((uint16_t)sizeof(sock_test_payload))

void socket_self_test(void)
{
    int pass = 0, fail = 0;

    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  SOCKET LAYER SELF-TEST  (loopback 127.0.0.1)\n");
    uart_puts("=============================================================\n");

    /* ----------------------------------------------------------------
     *  Test 1: sock_create — create a UDP/AF_INET socket
     * ---------------------------------------------------------------- */
    uart_puts("[sock_test] --- Test 1: sock_create(AF_INET, SOCK_DGRAM, 0) ---\n");

    int fd = sock_create(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        uart_puts("[sock_test]   FAIL: sock_create returned err=");
        uart_putu((uint64_t)(int64_t)-fd);
        uart_puts("\n");
        fail++;
        goto sock_done;   /* cannot continue without a valid fd */
    }
    uart_puts("[sock_test]   sock_create returned fd=");
    uart_putu((uint64_t)fd);
    uart_puts("\n");
    uart_puts("[sock_test]   PASS: socket created\n\n");
    pass++;

    /* ----------------------------------------------------------------
     *  Test 2: sock_bind — bind to 127.0.0.1:8888
     * ---------------------------------------------------------------- */
    uart_puts("[sock_test] --- Test 2: sock_bind(fd, 127.0.0.1:8888) ---\n");

    {
        sockaddr_in_t local = sockaddr_in_make(IPV4(127, 0, 0, 1), SOCK_TEST_PORT);
        int ret = sock_bind(fd, &local);
        if (ret != SOCK_ERR_OK) {
            uart_puts("[sock_test]   FAIL: sock_bind returned err=");
            uart_putu((uint64_t)(int64_t)-ret);
            uart_puts("\n");
            fail++;
            goto sock_cleanup;
        }
        uart_puts("[sock_test]   PASS: bound to 127.0.0.1:8888\n\n");
        pass++;
    }

    /* ----------------------------------------------------------------
     *  Test 3: sock_sendto — send payload to ourselves
     *
     *  Loopback is fully synchronous: udp_send() drives the frame all
     *  the way through the stack and fires socket_udp_dispatch() before
     *  returning, so the datagram is already in the RX ring when
     *  sock_sendto() returns.
     * ---------------------------------------------------------------- */
    uart_puts("[sock_test] --- Test 3: sock_sendto 6 bytes to 127.0.0.1:8888 ---\n");

    {
        sockaddr_in_t dest = sockaddr_in_make(IPV4(127, 0, 0, 1), SOCK_TEST_PORT);
        int ret = sock_sendto(fd, sock_test_payload, SOCK_TEST_LEN, &dest);
        if (ret != (int)SOCK_TEST_LEN) {
            uart_puts("[sock_test]   FAIL: sock_sendto returned ");
            uart_putu((uint64_t)(int64_t)ret);
            uart_puts(" (expected ");
            uart_putu(SOCK_TEST_LEN);
            uart_puts(")\n");
            fail++;
            goto sock_cleanup;
        }
        uart_puts("[sock_test]   sock_sendto returned ");
        uart_putu((uint64_t)ret);
        uart_puts(" bytes\n");
        uart_puts("[sock_test]   PASS: sendto succeeded\n\n");
        pass++;
    }

    /* ----------------------------------------------------------------
     *  Test 4: sock_recvfrom — read the datagram back
     * ---------------------------------------------------------------- */
    uart_puts("[sock_test] --- Test 4: sock_recvfrom ---\n");

    {
        uint8_t rxbuf[64];
        sockaddr_in_t from;
        int ret = sock_recvfrom(fd, rxbuf, (uint16_t)sizeof(rxbuf), &from);
        if (ret != (int)SOCK_TEST_LEN) {
            uart_puts("[sock_test]   FAIL: sock_recvfrom returned ");
            uart_putu((uint64_t)(int64_t)ret);
            uart_puts(" (expected ");
            uart_putu(SOCK_TEST_LEN);
            uart_puts(")\n");
            fail++;
            goto sock_cleanup;
        }

        /* Verify payload content */
        int mismatch = 0;
        for (int i = 0; i < (int)SOCK_TEST_LEN; i++) {
            if (rxbuf[i] != sock_test_payload[i]) {
                uart_puts("[sock_test]   FAIL: payload mismatch at byte ");
                uart_putu((uint64_t)i);
                uart_puts(": got 0x");
                uart_putu(rxbuf[i]);
                uart_puts(", expected 0x");
                uart_putu(sock_test_payload[i]);
                uart_puts("\n");
                mismatch = 1;
                fail++;
                break;
            }
        }

        /* Verify source address is 127.0.0.1 */
        if (!mismatch && !ipv4_equal(from.sin_addr, IPV4(127, 0, 0, 1))) {
            uart_puts("[sock_test]   FAIL: sender address is not 127.0.0.1\n");
            mismatch = 1;
            fail++;
        }

        if (!mismatch) {
            uart_puts("[sock_test]   received \"SOCKET\" from 127.0.0.1\n");
            uart_puts("[sock_test]   PASS: recvfrom succeeded\n\n");
            pass++;
        }
    }

sock_cleanup:
    /* ----------------------------------------------------------------
     *  Test 5: sock_close — release resources
     * ---------------------------------------------------------------- */
    uart_puts("[sock_test] --- Test 5: sock_close ---\n");

    {
        int ret = sock_close(fd);
        if (ret != SOCK_ERR_OK) {
            uart_puts("[sock_test]   FAIL: sock_close returned err=");
            uart_putu((uint64_t)(int64_t)-ret);
            uart_puts("\n");
            fail++;
        } else {
            uart_puts("[sock_test]   PASS: sock_close succeeded\n\n");
            pass++;
        }
    }

sock_done:
    uart_puts("=============================================================\n");
    uart_puts("  SOCKET SELF-TEST COMPLETE  pass=");
    uart_putu((uint64_t)pass);
    uart_puts("  fail=");
    uart_putu((uint64_t)fail);
    uart_puts("\n");
    uart_puts("=============================================================\n\n");
}
