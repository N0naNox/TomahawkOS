#pragma once
/**
 * @file dhcp.h
 * @brief Minimal DHCP client (Discover → Offer → Request → Ack)
 */

#include "net_device.h"

/**
 * @brief Perform a DHCP four-way handshake on @p dev.
 *
 * Broadcasts a DHCP Discover, waits for an Offer, sends a Request,
 * and waits for an Acknowledgement.  On success the device's IP,
 * netmask, and gateway fields are updated via net_device_set_ip().
 *
 * This is a synchronous, poll-mode call intended to be made once during
 * kernel boot after the NIC is up.
 *
 * @param dev  The network device to configure.
 * @return  0 on success, -1 on timeout or error.
 */
int dhcp_discover(net_device_t *dev);
