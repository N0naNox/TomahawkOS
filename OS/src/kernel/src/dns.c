/**
 * @file dns.c
 * @brief Minimal DNS A-record resolver
 *
 * Wire format overview (RFC 1035):
 *
 *  ┌─────────┬──────────┬─────────┬─────────┬─────────┬─────────┐
 *  │   ID    │  FLAGS   │ QDCOUNT │ ANCOUNT │ NSCOUNT │ ARCOUNT │
 *  │  (2 B)  │  (2 B)   │  (2 B)  │  (2 B)  │  (2 B)  │  (2 B)  │
 *  └─────────┴──────────┴─────────┴─────────┴─────────┴─────────┘
 *
 *  Question: label-encoded name | QTYPE=A(1) | QCLASS=IN(1)
 *  Answer  : name | TYPE | CLASS | TTL(4) | RDLENGTH | RDATA
 *
 *  Compressed names use a 2-byte pointer: 0xC0xx where xx is the
 *  byte offset from the start of the DNS message.
 */

#include "include/dns.h"
#include "include/udp.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/net_rx.h"
#include <uart.h>
#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 *  Constants
 * ==================================================================== */

/* QEMU's built-in virtual DNS server */
#define DNS_SERVER_IP   ((ipv4_addr_t){.bytes = {10, 0, 2, 3}})

/* Fallback: Google's public DNS */
#define DNS_FALLBACK_IP ((ipv4_addr_t){.bytes = {8, 8, 8, 8}})

#define DNS_SERVER_PORT  53u
#define DNS_CLIENT_PORT  1053u   /* local ephemeral port for DNS queries */

#define DNS_QUERY_ID     0xAB12u

/* ====================================================================
 *  Module state (written by callback, read by dns_resolve)
 * ==================================================================== */

static volatile int   dns_got_reply = 0;
static ipv4_addr_t    dns_result;

/* ====================================================================
 *  Helpers
 * ==================================================================== */

/* Encode a dotted hostname into DNS label format.
 * E.g. "abc.com" → \x03abc\x03com\x00
 * Returns the number of bytes written, or 0 on error.               */
static uint16_t encode_name(const char *host, uint8_t *out, uint16_t maxlen)
{
    uint16_t off = 0;

    while (*host) {
        /* Find the length of this label */
        uint16_t label_len = 0;
        const char *p = host;
        while (*p && *p != '.') { p++; label_len++; }

        if (label_len == 0 || label_len > 63) return 0;
        if (off + 1 + label_len + 1 > maxlen) return 0;

        out[off++] = (uint8_t)label_len;
        for (uint16_t i = 0; i < label_len; i++) out[off++] = (uint8_t)host[i];

        host += label_len;
        if (*host == '.') host++;
    }

    out[off++] = 0;   /* root label */
    return off;
}

/* Skip a DNS name field (handles compressed pointers).
 * Returns the number of bytes consumed in the message from `pos`,
 * without following the pointer.                                     */
static uint16_t skip_name(const uint8_t *msg, uint16_t msg_len, uint16_t pos)
{
    uint16_t start = pos;
    while (pos < msg_len) {
        uint8_t c = msg[pos];
        if (c == 0) { pos++; break; }
        if ((c & 0xC0) == 0xC0) { pos += 2; break; }   /* compressed pointer */
        pos += 1 + c;   /* skip label */
    }
    return (uint16_t)(pos - start);
}

/* ====================================================================
 *  UDP receive callback
 * ==================================================================== */

static void dns_rx_callback(struct net_device *dev,
                             ipv4_addr_t src_ip,
                             uint16_t src_port,
                             const uint8_t *data,
                             uint16_t data_len)
{
    (void)dev;
    (void)src_ip;
    if (dns_got_reply) return;
    if (src_port != DNS_SERVER_PORT) return;
    if (data_len < 12) return;

    /* Check transaction ID */
    uint16_t rxid = (uint16_t)((data[0] << 8) | data[1]);
    if (rxid != DNS_QUERY_ID) return;

    /* Check QR=1 (response) and RCODE=0 (no error) */
    uint8_t flags_hi = data[2], flags_lo = data[3];
    if (!(flags_hi & 0x80)) return;       /* QR bit must be 1 */
    if ((flags_lo & 0x0F) != 0) return;   /* RCODE != 0 → error */

    uint16_t qdcount = (uint16_t)((data[4]  << 8) | data[5]);
    uint16_t ancount = (uint16_t)((data[6]  << 8) | data[7]);

    if (ancount == 0) { uart_puts("[dns] no answer records\n"); return; }

    /* Skip the header (12 bytes) */
    uint16_t pos = 12;

    /* Skip question section */
    for (uint16_t q = 0; q < qdcount && pos < data_len; q++) {
        pos += skip_name(data, data_len, pos);   /* QNAME */
        pos += 4;                                /* QTYPE + QCLASS */
    }

    /* Walk answer records looking for TYPE=A, CLASS=IN */
    for (uint16_t a = 0; a < ancount && pos < data_len; a++) {
        pos += skip_name(data, data_len, pos);            /* NAME */
        if (pos + 10 > data_len) break;

        uint16_t rtype    = (uint16_t)((data[pos]   << 8) | data[pos+1]);
        uint16_t rclass   = (uint16_t)((data[pos+2] << 8) | data[pos+3]);
        /* TTL is data[pos+4..7], skip */
        uint16_t rdlength = (uint16_t)((data[pos+8] << 8) | data[pos+9]);
        pos += 10;

        if (rtype == 1 && rclass == 1 && rdlength == 4 && pos + 4 <= data_len) {
            /* A record — extract IPv4 address */
            dns_result.bytes[0] = data[pos];
            dns_result.bytes[1] = data[pos+1];
            dns_result.bytes[2] = data[pos+2];
            dns_result.bytes[3] = data[pos+3];

            uart_puts("[dns] resolved: ");
            for (int i = 0; i < 4; i++) {
                uart_putu(dns_result.bytes[i]);
                if (i < 3) uart_puts(".");
            }
            uart_puts("\n");

            dns_got_reply = 1;
            return;
        }

        pos += rdlength;   /* skip RDATA for non-A records */
    }

    uart_puts("[dns] no A record in response\n");
}

/* ====================================================================
 *  Public API
 * ==================================================================== */

/**
 * @brief Try to parse a dotted-decimal IP string.
 * @return 1 if @p s looks like a.b.c.d, 0 otherwise.
 */
static int parse_dotted_ip(const char *s, ipv4_addr_t *out)
{
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return 0;
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s++ - '0'); }
        if (v > 255) return 0;
        out->bytes[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return (*s == '\0' || *s == '/');
}

int dns_resolve(net_device_t *dev, const char *hostname, ipv4_addr_t *out)
{
    if (!hostname || !out || !dev) return -1;

    /* Fast path: already a numeric IP */
    if (parse_dotted_ip(hostname, out)) return 0;

    /* Build DNS query packet */
    static uint8_t pkt[512];
    uint16_t off = 0;

    /* Header */
    pkt[off++] = (uint8_t)(DNS_QUERY_ID >> 8);
    pkt[off++] = (uint8_t)(DNS_QUERY_ID);
    pkt[off++] = 0x01; pkt[off++] = 0x00;   /* flags: RD=1 */
    pkt[off++] = 0x00; pkt[off++] = 0x01;   /* QDCOUNT=1 */
    pkt[off++] = 0x00; pkt[off++] = 0x00;   /* ANCOUNT=0 */
    pkt[off++] = 0x00; pkt[off++] = 0x00;   /* NSCOUNT=0 */
    pkt[off++] = 0x00; pkt[off++] = 0x00;   /* ARCOUNT=0 */

    /* Question: QNAME */
    uint16_t name_len = encode_name(hostname, pkt + off, (uint16_t)(sizeof(pkt) - off - 4));
    if (name_len == 0) { uart_puts("[dns] hostname too long\n"); return -1; }
    off += name_len;

    /* QTYPE=A(1), QCLASS=IN(1) */
    pkt[off++] = 0x00; pkt[off++] = 0x01;
    pkt[off++] = 0x00; pkt[off++] = 0x01;

    /* Reset state and bind */
    dns_got_reply = 0;
    udp_bind(DNS_CLIENT_PORT, dns_rx_callback);

    uart_puts("[dns] querying for: ");
    uart_puts(hostname);
    uart_puts("\n");

    /* Send initial query to QEMU DNS.  The first send may fail because ARP for
     * the gateway (10.0.2.2) hasn't been resolved yet.  Pump the network for a
     * short while and retry a few times before switching to the fallback.     */
    ipv4_addr_t dns_server = DNS_SERVER_IP;

    /* Retry the first send up to 5 times with ARP warm-up between attempts. */
    for (int attempt = 0; attempt < 5; attempt++) {
        int rc = udp_send(dev, dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, pkt, off);
        if (rc == 0) break;   /* sent OK */
        /* ARP not ready — pump network briefly so ARP reply can arrive */
        for (int j = 0; j < 200000; j++) {
            net_device_poll_all();
            net_rx_process();
        }
    }

    /* Poll for response.  Periodically resend to the same server in case the
     * first attempt was dropped (ARP miss, transient queue-full, etc.).      */
    for (int i = 0; i < 6000000 && !dns_got_reply; i++) {
        net_device_poll_all();
        net_rx_process();

        /* Resend to primary DNS after 300 k and 1 M iters (ARP now cached) */
        if ((i == 300000 || i == 1000000) && !dns_got_reply) {
            udp_send(dev, dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, pkt, off);
        }

        /* Fall back to 8.8.8.8 only after 3 M iters with retries exhausted */
        if (i == 3000000 && !dns_got_reply) {
            uart_puts("[dns] retrying with 8.8.8.8...\n");
            dns_server = DNS_FALLBACK_IP;
            udp_send(dev, dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, pkt, off);
        }
        if (i == 4500000 && !dns_got_reply) {
            udp_send(dev, dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, pkt, off);
        }
    }

    udp_unbind(DNS_CLIENT_PORT);

    if (!dns_got_reply) {
        uart_puts("[dns] resolution timeout\n");
        return -1;
    }

    *out = dns_result;
    return 0;
}
