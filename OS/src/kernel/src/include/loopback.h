/**
 * @file loopback.h
 * @brief Loopback Network Interface  ("lo" / 127.0.0.1)
 *
 * The loopback device is a virtual NIC that connects a machine to
 * itself.  Any frame "transmitted" on lo is immediately received
 * back on the same interface, exercising the full Ethernet → IPv4
 * → ICMP/UDP path without any real hardware.
 *
 * Configuration:
 *   name    : "lo"
 *   MAC     : 00:00:00:00:00:00
 *   IPv4    : 127.0.0.1 / 255.0.0.0
 *   gateway : 0.0.0.0
 *
 * This is the first functional "NIC" in TomahawkOS and serves as
 * the validation target for the network stack architecture.
 */

#ifndef LOOPBACK_H
#define LOOPBACK_H

#include "net_device.h"

/**
 * @brief Initialise and register the loopback device.
 *
 * Creates a net_device named "lo" with IP 127.0.0.1,
 * pre-populates the ARP cache so that 127.0.0.1 resolves
 * immediately, and registers it with the device registry.
 *
 * Call from net_init() or kernel boot.
 */
void loopback_init(void);

/**
 * @brief Return a pointer to the loopback net_device.
 * @return Pointer to the "lo" device, or NULL if not yet initialised.
 */
net_device_t *loopback_dev(void);

#endif /* LOOPBACK_H */
