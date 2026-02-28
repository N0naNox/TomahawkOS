#pragma once
#include "net_device.h"
#include <stdint.h>

/**
 * @brief Perform an HTTP/1.0 GET request.
 *
 * @param dev      Network device to use.
 * @param hostname Domain name or dotted-decimal IP string.
 * @param port     TCP port (usually 80).
 * @param path     URI path (must start with '/'). Pass "/" for root.
 * @param out_buf  Caller-supplied buffer for the response body.
 * @param out_size Size of @p out_buf in bytes.
 *
 * @return Number of bytes written to @p out_buf (body only, NUL-terminated),
 *         or a negative error code:
 *         -1  DNS resolution failed
 *         -2  TCP connect failed
 *         -3  Response buffer overflow (body truncated to out_size-1)
 *         -4  Malformed / empty response
 */
int http_get(net_device_t *dev,
             const char   *hostname,
             uint16_t      port,
             const char   *path,
             char         *out_buf,
             uint32_t      out_size);
