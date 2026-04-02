/**
 * @file e1000.h
 * @brief Intel 82540EM (e1000) NIC driver
 *
 * Supports the QEMU "-device e1000" emulated NIC.
 * Poll-mode only (no IRQ); the timer tick calls net_device_poll_all()
 * which drains any received frames each tick.
 *
 * PCI ID: vendor 0x8086, device 0x100E
 *
 * Usage:
 *   e1000_init();          // scans PCI, registers "eth0" device
 *   net_device_up(eth0);   // starts the NIC
 */

#ifndef E1000_H
#define E1000_H

/**
 * @brief Scan PCI bus 0 for the e1000 NIC, initialise it, and register
 *        a net_device_t named "eth0" with the network stack.
 *
 * Does nothing (logs a message) if no e1000 is found.
 * Must be called after net_init() has set up the netbuf pool.
 */
void e1000_init(void);

#endif /* E1000_H */
