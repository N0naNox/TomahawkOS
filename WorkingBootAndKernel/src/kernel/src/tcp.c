/**
 * @file tcp.c
 * @brief Minimal synchronous TCP client — poll-mode, single connection
 *
 * Design constraints:
 *  • One active connection at a time (global pointer).
 *  • No retransmission (QEMU's virtual network is loss-free).
 *  • No congestion control, no window scaling.
 *  • All I/O is synchronous: the caller spins calling
 *    net_device_poll_all() + net_rx_process() until the expected
 *    state transition occurs or a timeout expires.
 */

#include "include/tcp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/net_rx.h"
#include "include/ipv4.h"
#include <uart.h>
#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 *  Active-connection registry (one slot)
 * ==================================================================== */

static tcp_conn_t *active_conn = NULL;

/* Rotating ephemeral port — avoids SLiRP TIME_WAIT collision on repeated calls */
static uint16_t next_ephemeral_port = TCP_EPHEMERAL_PORT;

void tcp_init(void)
{
    active_conn = NULL;
    next_ephemeral_port = TCP_EPHEMERAL_PORT;
}

/* ====================================================================
 *  Checksum helpers
 *
 *  The TCP checksum covers a 12-byte pseudo-header followed by the
 *  TCP header and payload.  We compute it with a dedicated function
 *  that accumulates the one's-complement sum correctly.
 * ==================================================================== */

/* One's-complement sum of a byte buffer into a 32-bit accumulator.
 * Reads 16-bit words in LITTLE-ENDIAN order, matching net_checksum()
 * and the native x86 byte order.  The Internet checksum algorithm is
 * byte-order-independent (RFC 1071), so the final result stored as a
 * uint16_t on a LE machine produces the correct on-wire bytes.      */
static uint32_t oc_sum(const void *buf, uint16_t len, uint32_t acc)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 1) {
        acc += (uint32_t)(p[0] | ((uint16_t)p[1] << 8));   /* LE read */
        p   += 2;
        len -= 2;
    }
    if (len == 1) acc += (uint32_t)p[0];   /* odd trailing byte */
    return acc;
}

static uint16_t oc_fold(uint32_t acc)
{
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return (uint16_t)~acc;
}

/**
 * @brief Compute the TCP segment checksum.
 *
 * @param src_ip   Sender IPv4 address
 * @param dst_ip   Destination IPv4 address
 * @param tcp      Pointer to TCP header (checksum field must be 0)
 * @param tcp_len  Total length of TCP segment (header + payload) in bytes
 */
static uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip,
                              const void *tcp, uint16_t tcp_len)
{
    uint32_t acc = 0;

    /* Pseudo-header: src(4) + dst(4) + zero(1) + proto(1) + tcp_len(2) */
    acc = oc_sum(src_ip.bytes, 4, acc);
    acc = oc_sum(dst_ip.bytes, 4, acc);
    uint8_t psh_tail[4] = { 0, 6 /* IP_PROTO_TCP */,
                             (uint8_t)(tcp_len >> 8),
                             (uint8_t)(tcp_len) };
    acc = oc_sum(psh_tail, 4, acc);

    /* TCP header + payload */
    acc = oc_sum(tcp, tcp_len, acc);

    return oc_fold(acc);
}

/* ====================================================================
 *  Low-level segment sender
 * ==================================================================== */

/**
 * @brief Build and transmit a TCP segment (no data payload).
 *
 * @param conn   Active connection (provides addressing, ports, window)
 * @param seq    Sequence number to use (network byte order AFTER htonl)
 * @param ack    Acknowledgement number (set in header and in flags if ACK)
 * @param flags  TCP_FLAG_* bitmask
 */
static int tcp_send_segment(tcp_conn_t *conn,
                             uint32_t seq, uint32_t ack,
                             uint8_t flags,
                             const uint8_t *payload, uint16_t pay_len)
{
    netbuf_t *nb = netbuf_alloc();
    if (!nb) return -1;

    netbuf_reserve(nb, NETBUF_HEADROOM);

    /* Append payload first (if any) */
    if (payload && pay_len > 0) {
        uint8_t *p = netbuf_put(nb, pay_len);
        for (uint16_t i = 0; i < pay_len; i++) p[i] = payload[i];
    }

    /* Prepend TCP header */
    tcp_header_t *tcp = (tcp_header_t *)netbuf_push(nb, TCP_HEADER_LEN);
    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq      = htonl(seq);
    tcp->ack_seq  = (flags & TCP_FLAG_ACK) ? htonl(ack) : 0;
    tcp->data_off = (TCP_HEADER_LEN / 4) << 4;   /* 5 << 4 = 0x50 */
    tcp->flags    = flags;
    tcp->window   = htons(TCP_WINDOW_SIZE);
    tcp->checksum = 0;
    tcp->urg_ptr  = 0;

    uint16_t seg_len = (uint16_t)(TCP_HEADER_LEN + pay_len);
    tcp->checksum = tcp_checksum(conn->dev->ip, conn->remote_ip,
                                 tcp, seg_len);

    int rc = ipv4_send(conn->dev, nb, conn->remote_ip, IP_PROTO_TCP);
    netbuf_free(nb);
    return rc;
}

/* ====================================================================
 *  Polling helper
 * ==================================================================== */

static void poll_network(void)
{
    net_device_poll_all();
    net_rx_process();
}

/* ====================================================================
 *  tcp_receive — called by ipv4_receive for protocol 6
 * ==================================================================== */

void tcp_receive(struct net_device *dev, struct netbuf *nb,
                 ipv4_addr_t src_ip, ipv4_addr_t dst_ip)
{
    (void)dst_ip;

    if (!active_conn) return;
    if (nb->len < TCP_HEADER_LEN) return;

    tcp_header_t *tcp = (tcp_header_t *)nb->data;

    /* Only handle packets for our active connection */
    if (ntohs(tcp->dst_port) != active_conn->local_port)  return;
    if (ntohs(tcp->src_port) != active_conn->remote_port) return;
    if (src_ip.addr != active_conn->remote_ip.addr) {
        /* Log mismatches — SLiRP might proxy from an unexpected source IP */
        uart_puts("[tcp] src_ip mismatch: got ");
        for (int _i = 0; _i < 4; _i++) {
            uart_putu(src_ip.bytes[_i]);
            if (_i < 3) uart_puts(".");
        }
        uart_puts(" expected ");
        for (int _i = 0; _i < 4; _i++) {
            uart_putu(active_conn->remote_ip.bytes[_i]);
            if (_i < 3) uart_puts(".");
        }
        uart_puts("\n");
        /* Accept it anyway — SLiRP may NAT the source address */
    }

    uint8_t  flags   = tcp->flags;
    uint32_t seg_seq = ntohl(tcp->seq);
    uint32_t seg_ack = ntohl(tcp->ack_seq);

    /* Calculate payload start and length */
    uint8_t  hdr_len    = (tcp->data_off >> 4) * 4;
    uint16_t payload_len = (nb->len > hdr_len) ? (nb->len - hdr_len) : 0;
    const uint8_t *payload = nb->data + hdr_len;

    /* RST → abort */
    if (flags & TCP_FLAG_RST) {
        uart_puts("[tcp] RST received — connection aborted\n");
        active_conn->state = TCP_STATE_CLOSED;
        return;
    }

    switch (active_conn->state) {

    /* ---- Waiting for SYN-ACK after our SYN ---- */
    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            /* Validate ACK: should ack our SYN (ISN+1) */
            if (seg_ack != active_conn->snd_next) {
                uart_puts("[tcp] SYN-ACK: bad ack number\n");
                return;
            }

            /* Record server's ISN, advance our rcv_next past SYN */
            active_conn->rcv_next = seg_seq + 1;

            /* Send ACK for the SYN-ACK */
            tcp_send_segment(active_conn,
                             active_conn->snd_next,
                             active_conn->rcv_next,
                             TCP_FLAG_ACK,
                             NULL, 0);

            active_conn->state = TCP_STATE_ESTABLISHED;
            uart_puts("[tcp] connection established\n");
        }
        break;

    /* ---- Receiving data / FIN ---- */
    case TCP_STATE_ESTABLISHED:
    case TCP_STATE_FIN_WAIT:

        /* Append in-order payload to receive buffer */
        if (payload_len > 0 && seg_seq == active_conn->rcv_next) {
            uint32_t space = TCP_RECV_BUF_SIZE - active_conn->recv_len;
            uint32_t copy  = payload_len;
            if (copy > space) copy = space;

            for (uint32_t i = 0; i < copy; i++)
                active_conn->recv_buf[active_conn->recv_len + i] = payload[i];
            active_conn->recv_len += copy;
            active_conn->rcv_next += payload_len;   /* advance past full seg */

            /* ACK the data */
            tcp_send_segment(active_conn,
                             active_conn->snd_next,
                             active_conn->rcv_next,
                             TCP_FLAG_ACK,
                             NULL, 0);
        }

        /* FIN from remote */
        if (flags & TCP_FLAG_FIN) {
            active_conn->rcv_next++;   /* FIN consumes one seq */
            active_conn->peer_fin = 1;

            /* Send FIN+ACK */
            tcp_send_segment(active_conn,
                             active_conn->snd_next,
                             active_conn->rcv_next,
                             TCP_FLAG_ACK | TCP_FLAG_FIN,
                             NULL, 0);
            active_conn->snd_next++;
            active_conn->state = TCP_STATE_TIME_WAIT;

            uart_puts("[tcp] peer closed, FIN-ACK sent\n");
        }
        break;

    default:
        break;
    }

    (void)dev;
    (void)seg_ack;
}

/* ====================================================================
 *  Public API
 * ==================================================================== */

int tcp_connect(tcp_conn_t *conn, net_device_t *dev,
                ipv4_addr_t dst_ip, uint16_t dst_port)
{
    /* Initialise the connection struct */
    for (uint32_t i = 0; i < sizeof(tcp_conn_t); i++)
        ((uint8_t *)conn)[i] = 0;

    /* Rotate ephemeral port so SLiRP doesn't hit old TIME_WAIT entries */
    uint16_t local_port = next_ephemeral_port;
    next_ephemeral_port = (next_ephemeral_port >= 49400u)
                        ? TCP_EPHEMERAL_PORT
                        : (uint16_t)(next_ephemeral_port + 1u);

    conn->dev         = dev;
    conn->remote_ip   = dst_ip;
    conn->local_port  = local_port;
    conn->remote_port = dst_port;
    conn->snd_next    = TCP_ISN;
    conn->state       = TCP_STATE_SYN_SENT;
    conn->recv_len    = 0;
    conn->peer_fin    = 0;

    /* Register as the active connection so tcp_receive() can update it */
    active_conn = conn;

    /* Send SYN — retry if ARP for the gateway is not yet resolved.  Mirror
     * the same retry-with-ARP-warmup logic used by cmd_udpsend.           */
    int syn_sent = 0;
    for (int attempt = 0; attempt < 10; attempt++) {
        int rc = tcp_send_segment(conn, conn->snd_next, 0, TCP_FLAG_SYN, NULL, 0);
        if (rc == 0) { syn_sent = 1; break; }
        /* Pump network so the ARP reply has a chance to arrive */
        for (int j = 0; j < 200000; j++) poll_network();
    }
    if (!syn_sent) {
        uart_puts("[tcp] SYN send failed (ARP?)\n");
        active_conn = NULL;
        conn->state = TCP_STATE_CLOSED;
        return -1;
    }
    conn->snd_next++;   /* SYN consumes one sequence number */

    uart_puts("[tcp] SYN sent (port ");
    uart_putu(local_port);
    uart_puts("), waiting for SYN-ACK...\n");

    /* Poll until ESTABLISHED or CLOSED.
     * Retransmit the SYN roughly every 1 M iterations (~1 second) so
     * QEMU SLiRP's upstream proxy has time to open the real connection.
     * Count rx frames for diagnostics.                                   */
    int rx_frames = 0;
    for (int i = 0; i < 80000000; i++) {
        net_device_poll_all();
        rx_frames += net_rx_process();   /* returns # frames processed */

        if (conn->state == TCP_STATE_ESTABLISHED) return 0;
        if (conn->state == TCP_STATE_CLOSED)       break;

        /* SYN retransmission: resend every ~1 M iterations */
        if (i > 0 && (i % 1000000) == 0) {
            uart_puts("[tcp] retransmitting SYN (rx_frames=");
            uart_putu((uint32_t)rx_frames);
            uart_puts(")\n");
            tcp_send_segment(conn, TCP_ISN, 0, TCP_FLAG_SYN, NULL, 0);
            /* snd_next stays at ISN+1; we reuse the same SYN seq */
        }
    }

    extern volatile uint32_t e1000_rx_count;
    uart_puts("[tcp] connect timeout (rx_frames=");
    uart_putu((uint32_t)rx_frames);
    uart_puts(", e1000_rx_total=");
    uart_putu(e1000_rx_count);
    uart_puts(")\n");
    active_conn = NULL;
    conn->state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_write(tcp_conn_t *conn, const uint8_t *data, uint16_t len)
{
    if (!conn || conn->state != TCP_STATE_ESTABLISHED) return -1;
    if (!data || len == 0) return 0;

    /* Send as a single PSH+ACK segment (HTTP requests fit in one MSS) */
    int rc = tcp_send_segment(conn,
                               conn->snd_next,
                               conn->rcv_next,
                               TCP_FLAG_PSH | TCP_FLAG_ACK,
                               data, len);
    if (rc == 0) conn->snd_next += len;
    return rc;
}

int tcp_recv_until_close(tcp_conn_t *conn)
{
    if (!conn) return -1;

    uart_puts("[tcp] waiting for data...\n");

    /* Poll until peer sends FIN (or RST / timeout).
     * Use a very generous limit — HTTP responses from real servers over
     * QEMU SLiRP can take several seconds for large bodies.             */
    for (int i = 0; i < 200000000 && !conn->peer_fin; i++) {
        poll_network();
        if (conn->state == TCP_STATE_CLOSED) break;
    }

    if (!conn->peer_fin && conn->state != TCP_STATE_TIME_WAIT) {
        uart_puts("[tcp] recv timeout\n");
    }

    uart_puts("[tcp] received ");
    uart_putu(conn->recv_len);
    uart_puts(" bytes\n");

    return (int)conn->recv_len;
}

void tcp_close(tcp_conn_t *conn)
{
    if (!conn) return;

    if (conn->state == TCP_STATE_ESTABLISHED) {
        /* We haven't received a FIN yet — send ours first */
        tcp_send_segment(conn,
                         conn->snd_next,
                         conn->rcv_next,
                         TCP_FLAG_FIN | TCP_FLAG_ACK,
                         NULL, 0);
        conn->snd_next++;
        conn->state = TCP_STATE_FIN_WAIT;

        /* Wait briefly for FIN-ACK from peer */
        for (int i = 0; i < 2000000 && conn->state == TCP_STATE_FIN_WAIT; i++)
            poll_network();
    }

    active_conn = NULL;
    conn->state = TCP_STATE_CLOSED;
    uart_puts("[tcp] connection closed\n");
}
