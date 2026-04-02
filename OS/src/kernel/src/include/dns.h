#pragma once
/**
 * @file dns.h
 * @brief Minimal DNS A-record resolver (UDP, synchronous, poll-mode)
 *
 * Sends a single DNS query to QEMU's virtual DNS server (10.0.2.3:53)
 * and waits for the first A record in the response.
 */

#include "net.h"
#include "net_device.h"
#include <stdint.h>

/**
 * @brief Resolve a hostname to an IPv4 address.
 *
 * Sends a DNS A-record query for @p hostname and blocks until a
 * response arrives or the request times out.
 *
 * If @p hostname is already a dotted-decimal IPv4 address (e.g.
 * "8.8.8.8") the parsing is skipped and the address is returned
 * directly without sending any packets.
 *
 * @param dev       NIC to use for the query.
 * @param hostname  Null-terminated hostname or "a.b.c.d" string.
 * @param out       Set to the resolved IPv4 address on success.
 * @return 0 on success, -1 on failure / timeout.
 */
int dns_resolve(net_device_t *dev, const char *hostname, ipv4_addr_t *out);
