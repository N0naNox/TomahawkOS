/**
 * @file http.c
 * @brief Minimal HTTP/1.0 GET client
 *
 * Performs:
 *   1. DNS A-record resolution of the hostname
 *   2. TCP three-way handshake to port 80
 *   3. Sends an HTTP/1.0 GET request
 *   4. Reads all data until the server closes the connection (FIN)
 *   5. Strips HTTP response headers, returns the body
 */

#include "include/http.h"
#include "include/dns.h"
#include "include/tcp.h"
#include "include/net_device.h"
#include <uart.h>
#include <stddef.h>

/* ====================================================================
 *  Internal helpers
 * ==================================================================== */

/** Append a string to buf at *off, don't exceed cap, NUL-terminate */
static void buf_append(char *buf, uint32_t cap, uint32_t *off, const char *src)
{
    uint32_t i = 0;
    while (src[i] && *off + 1 < cap) {
        buf[(*off)++] = src[i++];
    }
    buf[*off] = '\0';
}

/* ====================================================================
 *  Public API
 * ==================================================================== */

int http_get(net_device_t *dev,
             const char   *hostname,
             uint16_t      port,
             const char   *path,
             char         *out_buf,
             uint32_t      out_size)
{
    if (!dev || !hostname || !path || !out_buf || out_size == 0) return -4;
    if (port == 0) port = 80;

    out_buf[0] = '\0';

    /* ---- 1. Resolve hostname ---- */
    uart_puts("[http] resolving ");
    uart_puts(hostname);
    uart_puts("\n");

    ipv4_addr_t server_ip;
    if (dns_resolve(dev, hostname, &server_ip) != 0) {
        uart_puts("[http] DNS resolution failed\n");
        return -1;
    }

    /* ---- 2. TCP connect to port 80 ---- */
    uart_puts("[http] connecting to port ");
    uart_putu(port);
    uart_puts("\n");

    static tcp_conn_t conn;   /* static so it's not on the stack (large recv_buf) */
    if (tcp_connect(&conn, dev, server_ip, port) != 0) {
        uart_puts("[http] TCP connect timed out\n");
        return -2;
    }

    uart_puts("[http] connected, sending request\n");

    /* ---- 3. Build and send HTTP request ---- */
    /* "GET /path HTTP/1.0\r\nHost: hostname\r\nConnection: close\r\n\r\n" */
    static char req[1024];
    uint32_t roff = 0;
    buf_append(req, sizeof(req), &roff, "GET ");
    buf_append(req, sizeof(req), &roff, path);
    buf_append(req, sizeof(req), &roff, " HTTP/1.0\r\nHost: ");
    buf_append(req, sizeof(req), &roff, hostname);
    buf_append(req, sizeof(req), &roff, "\r\nAccept: */*\r\nConnection: close\r\n\r\n");

    if (tcp_write(&conn, (const uint8_t *)req, (uint16_t)roff) != 0) {
        uart_puts("[http] tcp_write failed\n");
        tcp_close(&conn);
        return -2;
    }

    uart_puts("[http] request sent, waiting for response\n");

    /* ---- 4. Read until server closes connection ---- */
    int recv_bytes = tcp_recv_until_close(&conn);
    tcp_close(&conn);

    if (recv_bytes < 0) {
        uart_puts("[http] recv failed\n");
        return -2;
    }

    uart_puts("[http] received ");
    uart_putu((uint32_t)recv_bytes);
    uart_puts(" bytes\n");

    if (recv_bytes == 0) {
        uart_puts("[http] empty response\n");
        return -4;
    }

    /* ---- 5. Strip HTTP headers — find "\r\n\r\n" ---- */
    const char *resp = (const char *)conn.recv_buf;
    const char *body = NULL;

    for (int i = 0; i <= recv_bytes - 4; i++) {
        if (resp[i]   == '\r' && resp[i+1] == '\n' &&
            resp[i+2] == '\r' && resp[i+3] == '\n') {
            body = resp + i + 4;
            break;
        }
    }

    if (!body) {
        /* No header/body separator — return everything */
        uart_puts("[http] no header separator found, returning all data\n");
        body = resp;
    }

    /* ---- 6. Copy body to out_buf ---- */
    uint32_t body_len = (uint32_t)(recv_bytes - (uint32_t)(body - resp));
    int truncated = 0;

    if (body_len >= out_size) {
        body_len  = out_size - 1;
        truncated = 1;
    }

    for (uint32_t i = 0; i < body_len; i++) out_buf[i] = body[i];
    out_buf[body_len] = '\0';

    if (truncated) {
        uart_puts("[http] response body truncated to fit buffer\n");
        return -3;
    }

    return (int)body_len;
}
