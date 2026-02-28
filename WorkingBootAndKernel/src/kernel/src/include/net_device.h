/**
 * @file net_device.h
 * @brief Network Device Abstraction  (L1 — NIC driver interface)
 *
 * Every physical or virtual NIC in TomahawkOS is represented by a
 * `net_device_t`.  It carries:
 *   • The device's MAC address
 *   • Its assigned IPv4 address, subnet mask, and default gateway
 *   • A vtable of driver callbacks  (`net_device_ops`)
 *   • TX/RX packet counters for diagnostics
 *
 * A concrete NIC driver (e.g. E1000, virtio-net) fills in the ops
 * struct and calls `net_device_register()` during probe.
 *
 * ─── Packet flow through a net_device ───
 *
 *   TX:  higher layers → ethernet_send_frame() → ops->send()
 *   RX:  NIC IRQ/poll  → ops->poll() fills netbuf → ethernet_receive()
 */

#ifndef NET_DEVICE_H
#define NET_DEVICE_H

#include "net.h"

/* Maximum number of NICs the system can track */
#define NET_MAX_DEVICES  4

/* Maximum device name length */
#define NET_DEV_NAME_LEN 16

/* Forward declaration */
struct netbuf;

/* ====================================================================
 *  NIC driver callbacks
 * ==================================================================== */

/**
 * @brief Operations that every NIC driver must implement.
 */
typedef struct net_device_ops {
    /**
     * @brief Transmit a fully-formed Ethernet frame.
     * @param dev   The network device.
     * @param nb    Packet buffer with a complete Ethernet frame
     *              (data points to the Ethernet header).
     * @return 0 on success, negative error code on failure.
     */
    int (*send)(struct net_device *dev, struct netbuf *nb);

    /**
     * @brief Poll the NIC for a received frame (non-blocking).
     * @param dev   The network device.
     * @param nb    Packet buffer to fill.  On success, `data` and `len`
     *              describe the received Ethernet frame.
     * @return 0 if a frame was received, -1 if nothing available.
     *
     * For interrupt-driven NICs this can instead be wired from the
     * ISR; calling code will use whichever model the driver exposes.
     */
    int (*poll)(struct net_device *dev, struct netbuf *nb);

    /**
     * @brief Start / enable the NIC (bring the link up).
     * @return 0 on success.
     */
    int (*start)(struct net_device *dev);

    /**
     * @brief Stop / disable the NIC (bring the link down).
     * @return 0 on success.
     */
    int (*stop)(struct net_device *dev);
} net_device_ops_t;

/* ====================================================================
 *  net_device — one NIC instance
 * ==================================================================== */

typedef struct net_device {
    /* ---- identity ---- */
    char             name[NET_DEV_NAME_LEN];  /* e.g. "eth0"          */
    mac_addr_t       mac;                     /* hardware MAC          */

    /* ---- L3 configuration (static for now, DHCP later) ---- */
    ipv4_addr_t      ip;       /* assigned IPv4 address              */
    ipv4_addr_t      netmask;  /* subnet mask                        */
    ipv4_addr_t      gateway;  /* default gateway                    */

    /* ---- driver vtable ---- */
    net_device_ops_t *ops;

    /* ---- driver-private data ---- */
    void             *priv;    /* driver stores its own state here  */

    /* ---- statistics ---- */
    uint64_t         tx_packets;
    uint64_t         tx_bytes;
    uint64_t         rx_packets;
    uint64_t         rx_bytes;
    uint64_t         tx_errors;
    uint64_t         rx_errors;

    /* ---- link state ---- */
    int              link_up;  /* non-zero if the link is active     */
    int              registered; /* non-zero if added to device list */
} net_device_t;

/* ====================================================================
 *  Device registry
 * ==================================================================== */

/**
 * @brief Register a NIC with the network stack.
 *
 * Called by the driver probe function after filling in:
 *   name, mac, ops, priv.
 * The IP/netmask/gateway are configured later (manually or DHCP).
 *
 * @return 0 on success, -1 if the device table is full.
 */
int net_device_register(net_device_t *dev);

/**
 * @brief Look up a registered device by name (e.g. "eth0").
 * @return Pointer to the device, or NULL if not found.
 */
net_device_t *net_device_get_by_name(const char *name);

/**
 * @brief Return the first registered (primary) network device.
 *
 * Convenience for the common single-NIC case.
 * @return Pointer to device, or NULL if none registered.
 */
net_device_t *net_device_get_default(void);

/**
 * @brief Return the number of registered NICs.
 */
int net_device_count(void);

/**
 * @brief Get a registered device by index (0 .. count-1).
 */
net_device_t *net_device_get(int index);

/**
 * @brief Configure the IP settings of a device.
 */
void net_device_set_ip(net_device_t *dev,
                       ipv4_addr_t ip,
                       ipv4_addr_t netmask,
                       ipv4_addr_t gateway);

/* ====================================================================
 *  Interface lifecycle
 * ==================================================================== */

/**
 * @brief Bring a network interface up (calls ops->start, sets link_up).
 *
 * Safe to call when the interface is already up (no-op).
 * @return 0 on success, -1 if dev is NULL, or the ops->start error code.
 */
int net_device_up(net_device_t *dev);

/**
 * @brief Bring a network interface down (calls ops->stop, clears link_up).
 *
 * Safe to call when the interface is already down (no-op).
 * @return 0 on success, -1 if dev is NULL, or the ops->stop error code.
 */
int net_device_down(net_device_t *dev);

/* ====================================================================
 *  Raw transmit
 * ==================================================================== */

/**
 * @brief Send a pre-built frame directly through the NIC driver.
 *
 * Unlike ethernet_send_frame(), this function does NOT prepend any
 * L2 header — it expects `nb->data` to already point at a fully
 * formed frame.  Useful for raw sockets and for the Ethernet layer
 * itself.  Updates tx_packets / tx_bytes / tx_errors.
 *
 * @param dev  Network device.
 * @param nb   Packet buffer with a complete frame.
 * @return 0 on success, negative on error.
 */
int net_device_transmit(net_device_t *dev, struct netbuf *nb);

/* ====================================================================
 *  Receive pump
 * ==================================================================== */

/**
 * @brief Poll every registered device for received frames.
 *
 * For each device whose ops->poll() callback returns a frame, the
 * frame is forwarded to ethernet_receive().  Polling stops per-device
 * when poll() returns -1 (no more frames available).
 *
 * Call this from a kernel thread or timer ISR for poll-driven NICs.
 * Interrupt-driven NICs never need this.
 */
void net_device_poll_all(void);

/* ====================================================================
 *  Routing helpers
 * ==================================================================== */

/**
 * @brief Find the device whose assigned IP matches `ip` exactly.
 * @return Pointer to the device, or NULL if no match.
 */
net_device_t *net_device_find_by_ip(ipv4_addr_t ip);

/**
 * @brief Select the best outgoing interface for a destination IP.
 *
 * Simple longest-prefix-match over registered devices:
 *   1. If `dst_ip` falls within a device's subnet  → return that device.
 *   2. If multiple subnets match, the first match wins (no metric yet).
 *   3. If no subnet matches, fall back to the device that has a non-zero
 *      gateway configured (default route).
 *   4. If still no candidate, return net_device_get_default().
 *
 * @param dst_ip Destination IPv4 address (network byte order).
 * @return Best device for this destination, or NULL if none registered.
 */
net_device_t *net_device_route(ipv4_addr_t dst_ip);

/* ====================================================================
 *  Diagnostics
 * ==================================================================== */

/**
 * @brief Print statistics for a single device to serial (UART).
 *
 * Prints: name, IP, MAC, link state, tx/rx packet/byte/error counts.
 */
void net_device_print_stats(net_device_t *dev);

/**
 * @brief Print statistics for all registered devices to serial (UART).
 */
void net_device_print_all_stats(void);

#endif /* NET_DEVICE_H */
