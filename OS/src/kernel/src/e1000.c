/**
 * @file e1000.c
 * @brief Intel 82540EM (e1000) NIC driver — poll-mode implementation
 *
 * ═══════════════════════════════════════════════════════════════════
 *  HOW IT WORKS
 * ═══════════════════════════════════════════════════════════════════
 *
 *  1. PCI scan — enumerate bus 0 via CF8/CFC port I/O.  Stop at the
 *     first device with vendor=0x8086, device=0x100E (82540EM).
 *
 *  2. BAR0 — 32-bit MMIO region.  Lower 4 bits are flags; mask them
 *     off to get the physical base address.  Since the kernel runs with
 *     the UEFI identity map (VA == PA for RAM and MMIO below 4 GB),
 *     we cast the physical address directly to a volatile uint32_t *.
 *
 *  3. Descriptor rings — 8 TX and 8 RX descriptors, statically
 *     allocated and 16-byte-aligned.  Each descriptor holds the
 *     physical address of a 2 KB packet buffer (also static).
 *
 *  4. MAC — read the first three 16-bit words of the EEPROM.
 *
 *  5. Receive — poll() checks the RX ring tail; if the DD (Descriptor
 *     Done) bit is set the packet is ready.  It copies data into the
 *     caller-supplied netbuf and re-arms the descriptor.
 *
 *  6. Transmit — send() copies the netbuf payload into the next free
 *     TX buffer, sets the descriptor, advances TDT, then spins on DD.
 *
 *  Timer tick → net_device_poll_all() → e1000_poll() handles RX.
 * ═══════════════════════════════════════════════════════════════════
 */

#include "include/e1000.h"
#include "include/net.h"
#include "include/net_device.h"
#include "include/hal_port_io.h"
#include "include/paging.h"
#include <uart.h>
#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 *  PCI configuration space access  (CF8 / CFC port I/O)
 * ==================================================================== */

#define PCI_CONFIG_ADDR  0xCF8u
#define PCI_CONFIG_DATA  0xCFCu

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | ((uint32_t)(reg & 0xFC));
    hal_outl(PCI_CONFIG_ADDR, addr);
    return hal_inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn,
                        uint8_t reg, uint32_t val)
{
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | ((uint32_t)(reg & 0xFC));
    hal_outl(PCI_CONFIG_ADDR, addr);
    hal_outl(PCI_CONFIG_DATA, val);
}

/* ====================================================================
 *  e1000 MMIO register offsets
 * ==================================================================== */

#define E1000_CTRL    0x0000u   /* Device Control                   */
#define E1000_STATUS  0x0008u   /* Device Status                     */
#define E1000_EERD    0x0014u   /* EEPROM Read                       */
#define E1000_ICR     0x00C0u   /* Interrupt Cause Read (clears)     */
#define E1000_IMS     0x00D0u   /* Interrupt Mask Set/Read           */
#define E1000_IMC     0x00D8u   /* Interrupt Mask Clear              */
#define E1000_RCTL    0x0100u   /* Receive Control                   */
#define E1000_TCTL    0x0400u   /* Transmit Control                  */
#define E1000_TIPG    0x0410u   /* Transmit IPG                      */
#define E1000_RDBAL   0x2800u   /* RX Descriptor Base Low            */
#define E1000_RDBAH   0x2804u   /* RX Descriptor Base High           */
#define E1000_RDLEN   0x2808u   /* RX Descriptor Ring Length (bytes) */
#define E1000_RDH     0x2810u   /* RX Descriptor Head (HW owned)     */
#define E1000_RDT     0x2818u   /* RX Descriptor Tail (SW writes)    */
#define E1000_TDBAL   0x3800u   /* TX Descriptor Base Low            */
#define E1000_TDBAH   0x3804u   /* TX Descriptor Base High           */
#define E1000_TDLEN   0x3808u   /* TX Descriptor Ring Length (bytes) */
#define E1000_TDH     0x3810u   /* TX Descriptor Head (HW owned)     */
#define E1000_TDT     0x3818u   /* TX Descriptor Tail (SW writes)    */
#define E1000_MTA     0x5200u   /* Multicast Table Array (128 dwords) */
#define E1000_RAL0    0x5400u   /* Receive Address Low [0]           */
#define E1000_RAH0    0x5404u   /* Receive Address High [0]          */

/* ---- CTRL bits ---- */
#define E1000_CTRL_LRST   (1u << 3)   /* Link Reset              */
#define E1000_CTRL_ASDE   (1u << 5)   /* Auto Speed Detection    */
#define E1000_CTRL_SLU    (1u << 6)   /* Set Link Up             */
#define E1000_CTRL_RST    (1u << 26)  /* Device Reset            */
#define E1000_CTRL_VME    (1u << 30)  /* VLAN Mode Enable        */

/* ---- RCTL bits ---- */
#define E1000_RCTL_EN     (1u << 1)    /* Receiver Enable         */
#define E1000_RCTL_BAM    (1u << 15)   /* Broadcast Accept Mode   */
#define E1000_RCTL_SECRC  (1u << 26)   /* Strip Ethernet CRC      */
/* buffer size: 00 = 2048, 01 = 1024, 10 = 512, 11 = 256 B        */
#define E1000_RCTL_BSIZE_2048  0u

/* ---- TCTL bits ---- */
#define E1000_TCTL_EN     (1u << 1)    /* Transmit Enable         */
#define E1000_TCTL_PSP    (1u << 3)    /* Pad Short Packets       */
#define E1000_TCTL_CT_SHIFT   4u
#define E1000_TCTL_COLD_SHIFT 12u

/* ---- TX descriptor CMD bits ---- */
#define E1000_TXD_CMD_EOP   0x01u  /* End of Packet               */
#define E1000_TXD_CMD_IFCS  0x02u  /* Insert FCS / CRC            */
#define E1000_TXD_CMD_RS    0x08u  /* Report Status (set DD bit)  */

/* ---- Descriptor done bits ---- */
#define E1000_TXD_STA_DD  0x01u    /* TX: descriptor done         */
#define E1000_RXD_STA_DD  0x01u    /* RX: descriptor done         */
#define E1000_RXD_STA_EOP 0x02u    /* RX: end of packet           */

/* ---- EERD bits ---- */
#define E1000_EERD_START  (1u << 0)
#define E1000_EERD_DONE   (1u << 4)
#define E1000_EERD_ADDR_SHIFT  8u
#define E1000_EERD_DATA_SHIFT  16u

/* ---- RAH bits ---- */
#define E1000_RAH_AV  (1u << 31)   /* Address Valid               */

/* ====================================================================
 *  Descriptor structures
 * ==================================================================== */

/* Legacy TX descriptor — 16 bytes */
typedef struct __attribute__((packed)) {
    uint64_t addr;      /* physical address of packet buffer   */
    uint16_t length;    /* packet length in bytes              */
    uint8_t  cso;       /* checksum offset (unused)            */
    uint8_t  cmd;       /* command flags                       */
    uint8_t  sta;       /* status (DD bit set by HW when done) */
    uint8_t  css;       /* checksum start (unused)             */
    uint16_t special;   /* unused                              */
} e1000_tx_desc_t;

/* Legacy RX descriptor — 16 bytes */
typedef struct __attribute__((packed)) {
    uint64_t addr;      /* physical address of packet buffer   */
    uint16_t length;    /* bytes written by HW (set on receive)*/
    uint16_t checksum;  /* packet checksum (ignored)           */
    uint8_t  status;    /* status flags (DD, EOP)              */
    uint8_t  errors;    /* error flags                         */
    uint16_t special;   /* unused                              */
} e1000_rx_desc_t;

/* ====================================================================
 *  Driver configuration
 * ==================================================================== */

#define E1000_TX_RING  8
#define E1000_RX_RING  8
#define E1000_BUF_SIZE 2048

/* ====================================================================
 *  Driver state (static singleton — one e1000 per machine)
 * ==================================================================== */

static volatile uint32_t *e1000_mmio = NULL;   /* MMIO base pointer    */

/* Descriptor rings — 16-byte aligned for the e1000 DMA engine */
static e1000_tx_desc_t tx_ring[E1000_TX_RING] __attribute__((aligned(16)));
static e1000_rx_desc_t rx_ring[E1000_RX_RING] __attribute__((aligned(16)));

/* Packet data buffers — one per descriptor */
static uint8_t tx_buf[E1000_TX_RING][E1000_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t rx_buf[E1000_RX_RING][E1000_BUF_SIZE] __attribute__((aligned(16)));

/* Ring indices maintained by the driver */
static uint32_t tx_tail = 0;

/*
 * RX ring has two SW pointers:
 *   rx_next — the next descriptor index to check for a received frame
 *             (tracks where HW will write next; starts at 0).
 *   rx_tail — the index we last wrote to RDT; the slot at rx_tail
 *             belongs to SW and has NOT been given to HW yet.
 * Invariant: HW owns descriptors [rx_next .. rx_tail-1] (circular).
 */
static uint32_t rx_next = 0;   /* SW read pointer  */
static uint32_t rx_tail = 0;   /* RDT write pointer */

/* The net_device struct registered with the stack */
static net_device_t eth0_device;

/* Diagnostic: total frames received (check at tcp timeout) */
volatile uint32_t e1000_rx_count = 0;

/* ====================================================================
 *  MMIO helpers
 * ==================================================================== */

static inline uint32_t e1000_read(uint32_t reg)
{
    return e1000_mmio[reg >> 2];
}

static inline void e1000_write(uint32_t reg, uint32_t val)
{
    e1000_mmio[reg >> 2] = val;
}

/* ====================================================================
 *  EEPROM read
 * ==================================================================== */

static uint16_t e1000_eeprom_read(uint8_t word)
{
    e1000_write(E1000_EERD,
                ((uint32_t)word << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    uint32_t val;
    int tries = 100000;
    do {
        val = e1000_read(E1000_EERD);
        if (val & E1000_EERD_DONE) break;
        /* tiny spin */
        for (volatile int i = 0; i < 10; i++) {}
    } while (--tries > 0);

    return (uint16_t)(val >> E1000_EERD_DATA_SHIFT);
}

/* ====================================================================
 *  net_device_ops callbacks
 * ==================================================================== */

static int e1000_start(struct net_device *dev)
{
    (void)dev;
    uart_puts("[e1000] interface up\n");
    return 0;
}

static int e1000_stop(struct net_device *dev)
{
    (void)dev;
    uart_puts("[e1000] interface down\n");
    return 0;
}

/**
 * @brief Transmit one Ethernet frame.
 *
 * Copies `nb->data[0..nb->len-1]` into the next TX buffer, writes the
 * descriptor, advances TDT, then spins until the DD bit confirms the
 * NIC has DMA'd the packet.  Returns 0 on success.
 */
static int e1000_send(struct net_device *dev, struct netbuf *nb)
{
    (void)dev;
    if (!nb || nb->len == 0 || nb->len > E1000_BUF_SIZE) return -1;

    uint32_t idx = tx_tail;

    /* Wait for previous use of this slot to complete */
    int tries = 100000;
    while (!(tx_ring[idx].sta & E1000_TXD_STA_DD) && --tries > 0) {
        /* Check if this descriptor has never been used yet (sta==0
         * on first use means "not yet sent", DD only valid after first TX).
         * We skip the check for the very first send by testing addr. */
        if (tx_ring[idx].addr == 0) break;
        for (volatile int i = 0; i < 5; i++) {}
    }

    /* Copy frame into buffer */
    const uint8_t *src = nb->data;
    uint8_t *dst = tx_buf[idx];
    for (uint16_t i = 0; i < nb->len; i++) dst[i] = src[i];

    /* Set up descriptor */
    tx_ring[idx].addr   = (uint64_t)(uintptr_t)tx_buf[idx];
    tx_ring[idx].length = nb->len;
    tx_ring[idx].cso    = 0;
    tx_ring[idx].cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS
                        | E1000_TXD_CMD_RS;
    tx_ring[idx].sta    = 0;  /* clear DD so we can detect completion */
    tx_ring[idx].css    = 0;
    tx_ring[idx].special = 0;

    /* Advance tail (tells NIC to transmit) */
    tx_tail = (tx_tail + 1) % E1000_TX_RING;
    e1000_write(E1000_TDT, tx_tail);

    return 0;
}

/**
 * @brief Poll for one received Ethernet frame.
 *
 * Checks whether the HW has filled the current RX descriptor (DD bit
 * set).  If yes, copies the data into @p nb and re-arms the descriptor.
 *
 * @return 0 if a frame was received, -1 if the ring is empty.
 */
static int e1000_poll(struct net_device *dev, struct netbuf *nb)
{
    (void)dev;

    /* Check the descriptor HW should have filled next */
    uint32_t idx = rx_next;

    if (!(rx_ring[idx].status & E1000_RXD_STA_DD))
        return -1;   /* nothing ready */

    e1000_rx_count++;

    uint16_t len = rx_ring[idx].length;
    if (len > E1000_BUF_SIZE) len = E1000_BUF_SIZE;

    /* Copy frame into netbuf */
    uint8_t *dst = netbuf_put(nb, len);
    const uint8_t *src = rx_buf[idx];
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];

    /* Re-arm the descriptor: clear status, restore buffer address */
    rx_ring[idx].status = 0;
    rx_ring[idx].addr   = (uint64_t)(uintptr_t)rx_buf[idx];

    /* Give the re-armed descriptor back to HW by advancing RDT to idx */
    rx_tail = idx;
    e1000_write(E1000_RDT, rx_tail);

    /* Advance SW read pointer to the next descriptor */
    rx_next = (idx + 1) % E1000_RX_RING;

    return 0;
}

static net_device_ops_t e1000_ops = {
    .send  = e1000_send,
    .poll  = e1000_poll,
    .isr   = NULL,       /* poll-mode only */
    .start = e1000_start,
    .stop  = e1000_stop,
};

/* ====================================================================
 *  Hardware initialisation
 * ==================================================================== */

static void e1000_hw_init(void)
{
    /* ---- Reset the device ---- */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    /* Wait for reset to clear */
    for (volatile int i = 0; i < 100000; i++) {}
    while (e1000_read(E1000_CTRL) & E1000_CTRL_RST) {}

    /* ---- Set Link Up, disable VLAN ---- */
    uint32_t ctrl = e1000_read(E1000_CTRL);
    ctrl |=  E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~E1000_CTRL_LRST;
    ctrl &= ~E1000_CTRL_VME;
    e1000_write(E1000_CTRL, ctrl);

    /* ---- Disable all interrupts ---- */
    e1000_write(E1000_IMC, 0xFFFFFFFFu);
    (void)e1000_read(E1000_ICR);   /* flush */

    /* ---- Clear multicast table ---- */
    for (int i = 0; i < 128; i++)
        e1000_write(E1000_MTA + (uint32_t)(i * 4), 0);

    /* ====== RX setup ====== */

    /* Base address of RX ring (physical == virtual in this kernel) */
    uint64_t rx_phys = (uint64_t)(uintptr_t)rx_ring;
    e1000_write(E1000_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFFu));
    e1000_write(E1000_RDBAH, (uint32_t)(rx_phys >> 32));
    e1000_write(E1000_RDLEN, (uint32_t)(E1000_RX_RING * sizeof(e1000_rx_desc_t)));

    /* Initialise RX descriptors */
    for (int i = 0; i < E1000_RX_RING; i++) {
        rx_ring[i].addr   = (uint64_t)(uintptr_t)rx_buf[i];
        rx_ring[i].status = 0;
    }

    /* HW head = 0; give descriptors 0..(RING-1) to HW by setting RDT = RING-1.
     * SW will start reading from index 0 (rx_next = 0).              */
    e1000_write(E1000_RDH, 0);
    rx_next = 0;
    rx_tail = E1000_RX_RING - 1;
    e1000_write(E1000_RDT, rx_tail);

    /* RCTL: enable receiver, accept broadcasts, 2KB buffers, strip CRC */
    e1000_write(E1000_RCTL,
                E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC
                | E1000_RCTL_BSIZE_2048);

    /* ====== TX setup ====== */

    uint64_t tx_phys = (uint64_t)(uintptr_t)tx_ring;
    e1000_write(E1000_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFFu));
    e1000_write(E1000_TDBAH, (uint32_t)(tx_phys >> 32));
    e1000_write(E1000_TDLEN, (uint32_t)(E1000_TX_RING * sizeof(e1000_tx_desc_t)));

    /* Initialise TX descriptors (mark all as "done" so first use works) */
    for (int i = 0; i < E1000_TX_RING; i++) {
        tx_ring[i].addr = 0;
        tx_ring[i].sta  = E1000_TXD_STA_DD;  /* available */
    }

    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    tx_tail = 0;

    /* TCTL: enable transmitter, pad short packets, collision params */
    e1000_write(E1000_TCTL,
                E1000_TCTL_EN | E1000_TCTL_PSP
                | (0x10u << E1000_TCTL_CT_SHIFT)      /* collision threshold = 16 */
                | (0x40u << E1000_TCTL_COLD_SHIFT));   /* collision distance = 64   */

    /* TIPG: standard inter-packet gap for 802.3 copper */
    e1000_write(E1000_TIPG, 0x0060200Au);

    uart_puts("[e1000] hardware initialised\n");
}

/* ====================================================================
 *  PCI scan + init entry point
 * ==================================================================== */

#define E1000_VENDOR  0x8086u
#define E1000_DEVICE  0x100Eu   /* 82540EM Gigabit Ethernet Controller */

void e1000_init(void)
{
    /* Scan PCI bus 0, all devices 0-31, function 0 */
    uint8_t found_bus = 0, found_dev = 0;
    int found = 0;

    for (uint8_t d = 0; d < 32; d++) {
        uint32_t id = pci_read32(0, d, 0, 0x00);
        uint16_t vendor = (uint16_t)(id & 0xFFFF);
        uint16_t device = (uint16_t)(id >> 16);

        if (vendor == E1000_VENDOR && device == E1000_DEVICE) {
            found_bus = 0;
            found_dev = d;
            found = 1;
            uart_puts("[e1000] found 82540EM at PCI 0:");
            uart_putu(d);
            uart_puts(".0\n");
            break;
        }
    }

    if (!found) {
        uart_puts("[e1000] no 82540EM found — eth0 not available\n");
        return;
    }

    /* Enable PCI bus mastering + memory space access */
    uint32_t cmd = pci_read32(found_bus, found_dev, 0, 0x04);
    cmd |= (1u << 1) | (1u << 2);   /* memory space + bus master */
    pci_write32(found_bus, found_dev, 0, 0x04, cmd);

    /* Read BAR0: 32-bit memory BAR (bits 3:0 are flags, mask them) */
    uint32_t bar0 = pci_read32(found_bus, found_dev, 0, 0x10);
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0u;

    uart_puts("[e1000] BAR0 MMIO base = 0x");
    uart_putu(mmio_phys);
    uart_puts("\n");

    /* The e1000 register space is 128 KB = 32 pages.  Map it explicitly
     * with cache-disable (PCD) and write-through (PWT) so the CPU never
     * caches MMIO reads/writes, then install the pointer. */
    {
        uintptr_t cr3   = paging_get_current_cr3();
        uint64_t  flags = PTE_PRESENT | PTE_RW | PTE_PCD | PTE_PWT;
        int       map_rc = paging_map_range(cr3,
                               (uint64_t)mmio_phys,   /* vaddr == paddr */
                               (uintptr_t)mmio_phys,  /* paddr          */
                               32,                    /* 32 × 4 KB = 128 KB */
                               flags);
        if (map_rc != 0) {
            uart_puts("[e1000] MMIO mapping failed — aborting\n");
            return;
        }
        uart_puts("[e1000] MMIO mapped (uncached, 128 KB)\n");
    }

    /* VA == PA in this kernel (identity map ensured above) */
    e1000_mmio = (volatile uint32_t *)(uintptr_t)mmio_phys;

    /* Hardware reset + configuration */
    e1000_hw_init();

    /* Read MAC from EEPROM (three 16-bit words) */
    uint16_t w0 = e1000_eeprom_read(0);
    uint16_t w1 = e1000_eeprom_read(1);
    uint16_t w2 = e1000_eeprom_read(2);

    mac_addr_t mac;
    mac.bytes[0] = (uint8_t)(w0 & 0xFF);
    mac.bytes[1] = (uint8_t)(w0 >> 8);
    mac.bytes[2] = (uint8_t)(w1 & 0xFF);
    mac.bytes[3] = (uint8_t)(w1 >> 8);
    mac.bytes[4] = (uint8_t)(w2 & 0xFF);
    mac.bytes[5] = (uint8_t)(w2 >> 8);

    uart_puts("[e1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        uint8_t b = mac.bytes[i];
        uint8_t hi = b >> 4, lo = b & 0xF;
        char c[3];
        c[0] = (char)(hi < 10 ? '0' + hi : 'a' + hi - 10);
        c[1] = (char)(lo < 10 ? '0' + lo : 'a' + lo - 10);
        c[2] = '\0';
        uart_puts(c);
        if (i < 5) uart_puts(":");
    }
    uart_puts("\n");

    /* Program MAC into Receive Address Register 0 */
    uint32_t ral = (uint32_t)mac.bytes[0]
                 | ((uint32_t)mac.bytes[1] << 8)
                 | ((uint32_t)mac.bytes[2] << 16)
                 | ((uint32_t)mac.bytes[3] << 24);
    uint32_t rah = (uint32_t)mac.bytes[4]
                 | ((uint32_t)mac.bytes[5] << 8)
                 | E1000_RAH_AV;
    e1000_write(E1000_RAL0, ral);
    e1000_write(E1000_RAH0, rah);

    /* Fill in the net_device */
    net_device_t *dev = &eth0_device;
    dev->name[0] = 'e'; dev->name[1] = 't'; dev->name[2] = 'h';
    dev->name[3] = '0'; dev->name[4] = '\0';
    dev->mac  = mac;
    dev->ops  = &e1000_ops;
    dev->priv = NULL;

    /* IP will be configured by DHCP later */
    dev->ip      = IPV4_ZERO;
    dev->netmask = IPV4_ZERO;
    dev->gateway = IPV4_ZERO;

    net_device_register(dev);
    net_device_up(dev);

    uart_puts("[e1000] eth0 registered and up\n");
}
