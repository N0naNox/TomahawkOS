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
#include "include/net_rx.h"
#include "include/net_tx.h"
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

/* ====================================================================
 *  net_device_iface_test — network interface abstraction self-test
 * ==================================================================== */

/* Convenience: check a condition, print PASS/FAIL and update counters. */
#define IFACE_CHECK(cond, msg)                          \
    do {                                                \
        if (cond) {                                     \
            uart_puts("[iface_test]   PASS: " msg "\n"); \
            pass++;                                     \
        } else {                                        \
            uart_puts("[iface_test]   FAIL: " msg "\n"); \
            fail++;                                     \
        }                                               \
    } while (0)

void net_device_iface_test(void)
{
    int pass = 0, fail = 0;

    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  NET INTERFACE ABSTRACTION SELF-TEST\n");
    uart_puts("=============================================================\n");

    net_device_t *lo = loopback_dev();
    if (!lo) {
        uart_puts("[iface_test] ABORT: loopback device not found\n");
        return;
    }

    /* ----------------------------------------------------------------
     *  Test 1: net_device_find_by_ip — exact IP match
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 1: net_device_find_by_ip(127.0.0.1) ---\n");
    {
        net_device_t *found = net_device_find_by_ip(IPV4(127, 0, 0, 1));
        IFACE_CHECK(found == lo,
            "find_by_ip(127.0.0.1) returns loopback device");

        net_device_t *none = net_device_find_by_ip(IPV4(192, 168, 1, 1));
        IFACE_CHECK(none == NULL,
            "find_by_ip(192.168.1.1) returns NULL (not registered)");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 2: net_device_route — subnet match and default fallback
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 2: net_device_route ---\n");
    {
        /* 127.x.x.x is in lo's subnet (255.0.0.0) — should return lo */
        net_device_t *r_lo = net_device_route(IPV4(127, 0, 0, 1));
        IFACE_CHECK(r_lo == lo,
            "route(127.0.0.1) selects loopback (on-link)");

        net_device_t *r2 = net_device_route(IPV4(127, 255, 255, 255));
        IFACE_CHECK(r2 == lo,
            "route(127.255.255.255) selects loopback (same /8 subnet)");

        /*
         * Off-link address: lo has no gateway, so net_device_route()
         * falls back to net_device_get_default() which is also lo.
         * Just verify a non-NULL device comes back.
         */
        net_device_t *r_off = net_device_route(IPV4(8, 8, 8, 8));
        IFACE_CHECK(r_off != NULL,
            "route(8.8.8.8) returns a non-NULL fallback device");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 3: net_device_down — bring loopback down
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 3: net_device_down ---\n");
    {
        int ret = net_device_down(lo);
        IFACE_CHECK(ret == 0,       "net_device_down returns 0");
        IFACE_CHECK(!lo->link_up,   "link_up cleared after down");

        /* Idempotency: calling down on an already-down device is a no-op */
        int ret2 = net_device_down(lo);
        IFACE_CHECK(ret2 == 0,      "net_device_down idempotent (double-down)");
        IFACE_CHECK(!lo->link_up,   "link_up still 0 after double-down");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 4: net_device_transmit — rejects frames while link is down
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 4: net_device_transmit (link down) ---\n");
    {
        uint64_t tx_err_before = lo->tx_errors;

        /* Build a minimal stub netbuf (no stack allocation needed) */
        netbuf_t *nb = netbuf_alloc();
        if (!nb) {
            uart_puts("[iface_test]   SKIP: netbuf pool exhausted\n");
        } else {
            netbuf_put(nb, 14);   /* fake 14-byte Ethernet header */

            int ret = net_device_transmit(lo, nb);
            IFACE_CHECK(ret != 0,
                "net_device_transmit fails when link is down");
            IFACE_CHECK(lo->tx_errors == tx_err_before + 1,
                "tx_errors incremented on link-down rejection");

            netbuf_free(nb);
        }
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 5: net_device_up — restore loopback
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 5: net_device_up ---\n");
    {
        int ret = net_device_up(lo);
        IFACE_CHECK(ret == 0,      "net_device_up returns 0");
        IFACE_CHECK(lo->link_up,   "link_up set after up");

        /* Idempotency */
        int ret2 = net_device_up(lo);
        IFACE_CHECK(ret2 == 0,     "net_device_up idempotent (double-up)");
        IFACE_CHECK(lo->link_up,   "link_up still 1 after double-up");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 6: net_device_transmit — succeeds and updates counters
     *
     *  We send a real UDP datagram through sock_sendto, which routes
     *  via lo → net_device_transmit → lo_send → ethernet_receive.
     *  Then we observe that tx_packets advanced, proving the stat path.
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 6: net_device_transmit (link up, via sendto) ---\n");
    {
        uint64_t tx_before = lo->tx_packets;

        /* Use the socket layer as a high-level driver for the frame */
        int fd = sock_create(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            uart_puts("[iface_test]   SKIP: sock_create failed\n");
        } else {
            sockaddr_in_t bind_addr = sockaddr_in_make(IPV4(127, 0, 0, 1), 9010);
            sock_bind(fd, &bind_addr);

            sockaddr_in_t dest = sockaddr_in_make(IPV4(127, 0, 0, 1), 9010);
            const uint8_t probe[] = { 'T', 'X' };
            sock_sendto(fd, probe, sizeof(probe), &dest);
            sock_close(fd);

            IFACE_CHECK(lo->tx_packets > tx_before,
                "tx_packets advanced after net_device_transmit call");
        }
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 7: net_device_poll_all — smoke-test (lo always returns -1)
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 7: net_device_poll_all (smoke) ---\n");
    {
        /* Should complete without crashing. lo's poll always returns -1,
         * so no frames are injected — we just verify it doesn't hang. */
        net_device_poll_all();
        uart_puts("[iface_test]   PASS: net_device_poll_all returned cleanly\n");
        pass++;
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 8: net_device_print_stats / net_device_print_all_stats
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 8: print_stats (visual) ---\n");
    {
        uart_puts("[iface_test]   net_device_print_stats(lo):\n");
        net_device_print_stats(lo);

        uart_puts("[iface_test]   net_device_print_all_stats():\n");
        net_device_print_all_stats();

        uart_puts("[iface_test]   PASS: print_stats returned without crash\n");
        pass++;
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  NULL-safety: NULL device pointer must not crash
     * ---------------------------------------------------------------- */
    uart_puts("[iface_test] --- Test 9: NULL-safety ---\n");
    {
        int r1 = net_device_up(NULL);
        IFACE_CHECK(r1 == -1, "net_device_up(NULL) returns -1");

        int r2 = net_device_down(NULL);
        IFACE_CHECK(r2 == -1, "net_device_down(NULL) returns -1");

        int r3 = net_device_transmit(NULL, NULL);
        IFACE_CHECK(r3 == -1, "net_device_transmit(NULL,NULL) returns -1");

        net_device_t *r4 = net_device_find_by_ip(IPV4(0, 0, 0, 0));
        IFACE_CHECK(r4 == NULL, "find_by_ip(0.0.0.0) returns NULL");

        /* print_stats(NULL) must not crash */
        net_device_print_stats(NULL);
        uart_puts("[iface_test]   PASS: print_stats(NULL) returned cleanly\n");
        pass++;
    }
    uart_puts("\n");

    uart_puts("=============================================================\n");
    uart_puts("  NET INTERFACE ABSTRACTION TEST COMPLETE  pass=");
    uart_putu((uint64_t)pass);
    uart_puts("  fail=");
    uart_putu((uint64_t)fail);
    uart_puts("\n");
    uart_puts("=============================================================\n\n");
}

/* ====================================================================
 *  RX packet path self-test
 * ==================================================================== */

#define RX_CHECK(cond, msg) \
    do { \
        if (cond) { \
            uart_puts("[rx_test]   PASS: " msg "\n"); \
            pass++; \
        } else { \
            uart_puts("[rx_test]   FAIL: " msg "\n"); \
            fail++; \
        } \
    } while (0)

void net_rx_path_test(void)
{
    int pass = 0, fail = 0;

    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  RX PACKET PATH TEST\n");
    uart_puts("=============================================================\n");

    /* Grab the loopback device for use as a stand-in NIC */
    net_device_t *lo = net_device_get_by_name("lo");

    /* ----------------------------------------------------------------
     *  Test 1: pending count is 0 before any enqueue
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 1: pending() == 0 at start ---\n");
    {
        int pending = net_rx_pending();
        RX_CHECK(pending == 0, "net_rx_pending() == 0 before enqueue");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 2: enqueue one frame — returns 0 (success)
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 2: enqueue one frame ---\n");
    {
        netbuf_t *nb = netbuf_alloc();
        RX_CHECK(nb != NULL, "netbuf_alloc() succeeded");

        if (nb) {
            int ret = net_rx_enqueue(lo, nb);
            RX_CHECK(ret == 0, "net_rx_enqueue() returned 0 (success)");

            /* --------------------------------------------------------
             *  Test 3: pending count is now 1
             * -------------------------------------------------------- */
            uart_puts("[rx_test] --- Test 3: pending() == 1 after enqueue ---\n");
            int pending = net_rx_pending();
            RX_CHECK(pending == 1, "net_rx_pending() == 1 after enqueue");
        }
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 4: process() drains the one queued entry (returns 1)
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 4: process() returns 1 ---\n");
    {
        int n = net_rx_process();
        RX_CHECK(n == 1, "net_rx_process() returned 1 (one frame processed)");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 5: pending count returns to 0 after process
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 5: pending() == 0 after process ---\n");
    {
        int pending = net_rx_pending();
        RX_CHECK(pending == 0, "net_rx_pending() == 0 after process");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 6: net_rx_print_stats() smoke-test
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 6: net_rx_print_stats() (visual) ---\n");
    {
        net_rx_print_stats();
        uart_puts("[rx_test]   PASS: net_rx_print_stats() returned cleanly\n");
        pass++;
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 7: ring-full — fill every slot, then verify drop
     *
     *  We pass NULL for the netbuf pointer so no real memory is
     *  consumed and net_rx_process() skips ethernet_receive() safely
     *  (it guards with `if (entry.dev && entry.nb)`).
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 7: ring-full drop path ---\n");
    {
        /* Fill all NET_RX_RING_SIZE slots */
        int fill_ok = 1;
        for (int i = 0; i < NET_RX_RING_SIZE; i++) {
            if (net_rx_enqueue(lo, NULL) != 0) {
                fill_ok = 0;
                break;
            }
        }
        RX_CHECK(fill_ok, "filled all ring slots without error");

        /* One more enqueue must fail with -1 (ring full / drop) */
        int ret = net_rx_enqueue(lo, NULL);
        RX_CHECK(ret == -1, "(ring+1)th enqueue returns -1 (drop)");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 8: drain the ring after the full test
     * ---------------------------------------------------------------- */
    uart_puts("[rx_test] --- Test 8: drain ring after full test ---\n");
    {
        int drained = net_rx_process();
        uart_puts("[rx_test]   drained=");
        uart_putu((uint64_t)drained);
        uart_puts(" frames\n");
        RX_CHECK(net_rx_pending() == 0, "ring empty after drain");
    }
    uart_puts("\n");

    uart_puts("=============================================================\n");
    uart_puts("  RX PACKET PATH TEST COMPLETE  pass=");
    uart_putu((uint64_t)pass);
    uart_puts("  fail=");
    uart_putu((uint64_t)fail);
    uart_puts("\n");
    uart_puts("=============================================================\n\n");
}

/* ====================================================================
 *  TX packet path self-test
 * ==================================================================== */

#define TX_CHECK(cond, msg) \
    do { \
        if (cond) { \
            uart_puts("[tx_test]   PASS: " msg "\n"); \
            pass++; \
        } else { \
            uart_puts("[tx_test]   FAIL: " msg "\n"); \
            fail++; \
        } \
    } while (0)

void net_tx_path_test(void)
{
    int pass = 0, fail = 0;

    uart_puts("\n");
    uart_puts("=============================================================\n");
    uart_puts("  TX PACKET PATH TEST\n");
    uart_puts("=============================================================\n");

    /* Grab the loopback device to act as the outgoing NIC */
    net_device_t *lo = net_device_get_by_name("lo");

    /* ----------------------------------------------------------------
     *  Test 1: pending count is 0 before any enqueue
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 1: pending() == 0 at start ---\n");
    {
        int pending = net_tx_pending();
        TX_CHECK(pending == 0, "net_tx_pending() == 0 before enqueue");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 2: enqueue one frame — returns 0 (success)
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 2: enqueue one frame ---\n");
    {
        netbuf_t *nb = netbuf_alloc();
        TX_CHECK(nb != NULL, "netbuf_alloc() succeeded");

        if (nb) {
            int ret = net_tx_enqueue(lo, nb);
            TX_CHECK(ret == 0, "net_tx_enqueue() returned 0 (success)");

            /* --------------------------------------------------------
             *  Test 3: pending count is now 1
             * -------------------------------------------------------- */
            uart_puts("[tx_test] --- Test 3: pending() == 1 after enqueue ---\n");
            int pending = net_tx_pending();
            TX_CHECK(pending == 1, "net_tx_pending() == 1 after enqueue");
        }
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 4: flush() drains the one queued entry (returns 1)
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 4: flush() returns 1 ---\n");
    {
        int n = net_tx_flush();
        TX_CHECK(n == 1, "net_tx_flush() returned 1 (one frame flushed)");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 5: pending count returns to 0 after flush
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 5: pending() == 0 after flush ---\n");
    {
        int pending = net_tx_pending();
        TX_CHECK(pending == 0, "net_tx_pending() == 0 after flush");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 6: net_tx_print_stats() smoke-test
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 6: net_tx_print_stats() (visual) ---\n");
    {
        net_tx_print_stats();
        uart_puts("[tx_test]   PASS: net_tx_print_stats() returned cleanly\n");
        pass++;
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 7: ring-full — fill every slot, then verify drop
     *
     *  We pass NULL for the netbuf pointer so no real memory is
     *  consumed and net_tx_flush() skips net_device_transmit() safely
     *  (it guards with `if (entry.dev && entry.nb)`).
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 7: ring-full drop path ---\n");
    {
        int fill_ok = 1;
        for (int i = 0; i < NET_TX_RING_SIZE; i++) {
            if (net_tx_enqueue(lo, NULL) != 0) {
                fill_ok = 0;
                break;
            }
        }
        TX_CHECK(fill_ok, "filled all ring slots without error");

        /* One more enqueue must fail with -1 (ring full / drop) */
        int ret = net_tx_enqueue(lo, NULL);
        TX_CHECK(ret == -1, "(ring+1)th enqueue returns -1 (drop)");
    }
    uart_puts("\n");

    /* ----------------------------------------------------------------
     *  Test 8: drain the ring after the full test
     * ---------------------------------------------------------------- */
    uart_puts("[tx_test] --- Test 8: drain ring after full test ---\n");
    {
        int drained = net_tx_flush();
        uart_puts("[tx_test]   drained=");
        uart_putu((uint64_t)drained);
        uart_puts(" frames\n");
        TX_CHECK(net_tx_pending() == 0, "ring empty after drain");
    }
    uart_puts("\n");

    uart_puts("=============================================================\n");
    uart_puts("  TX PACKET PATH TEST COMPLETE  pass=");
    uart_putu((uint64_t)pass);
    uart_puts("  fail=");
    uart_putu((uint64_t)fail);
    uart_puts("\n");
    uart_puts("=============================================================\n\n");
}
