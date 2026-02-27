/**
 * @file ata.h
 * @brief ATA PIO Disk Driver
 *
 * Minimal ATA PIO-mode driver for reading/writing 512-byte sectors.
 * Implements block_device_ops so it plugs into the existing block layer.
 *
 * The block layer uses 4096-byte blocks; the ATA driver bridges this
 * by reading/writing 8 sectors (8 * 512 = 4096) per block operation.
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Forward declaration */
struct block_device;

/* ========== ATA I/O Ports (Primary Bus) ========== */

#define ATA_PRIMARY_IO      0x1F0   /* Base I/O port                       */
#define ATA_PRIMARY_CTRL    0x3F6   /* Control/Alt-Status register         */

/* Registers relative to base I/O port */
#define ATA_REG_DATA        0       /* Data (16-bit, PIO read/write)       */
#define ATA_REG_ERROR       1       /* Error (read) / Features (write)     */
#define ATA_REG_SECCOUNT    2       /* Sector count                        */
#define ATA_REG_LBA_LO      3       /* LBA bits 0-7                        */
#define ATA_REG_LBA_MID     4       /* LBA bits 8-15                       */
#define ATA_REG_LBA_HI      5       /* LBA bits 16-23                      */
#define ATA_REG_DRIVE       6       /* Drive/Head (LBA bits 24-27 + flags) */
#define ATA_REG_STATUS      7       /* Status (read) / Command (write)     */
#define ATA_REG_COMMAND     7       /* Same port as status (write only)    */

/* Control register bits */
#define ATA_CTRL_NIEN       0x02    /* Disable interrupts                  */
#define ATA_CTRL_SRST       0x04    /* Software reset                      */

/* Status register bits */
#define ATA_SR_BSY          0x80    /* Busy                                */
#define ATA_SR_DRDY         0x40    /* Drive ready                         */
#define ATA_SR_DF           0x20    /* Drive fault                         */
#define ATA_SR_DSC          0x10    /* Drive seek complete                 */
#define ATA_SR_DRQ          0x08    /* Data request ready                  */
#define ATA_SR_CORR         0x04    /* Corrected data                      */
#define ATA_SR_IDX          0x02    /* Index                               */
#define ATA_SR_ERR          0x01    /* Error                               */

/* ATA Commands */
#define ATA_CMD_READ_PIO    0x20    /* READ SECTORS (PIO)                  */
#define ATA_CMD_WRITE_PIO   0x30    /* WRITE SECTORS (PIO)                 */
#define ATA_CMD_IDENTIFY    0xEC    /* IDENTIFY DEVICE                     */
#define ATA_CMD_FLUSH       0xE7    /* FLUSH CACHE                         */

/* Drive select values (for ATA_REG_DRIVE) */
#define ATA_DRIVE_MASTER    0xE0    /* Master + LBA mode                   */
#define ATA_DRIVE_SLAVE     0xF0    /* Slave + LBA mode                    */

/* Sector size */
#define ATA_SECTOR_SIZE     512
#define ATA_SECTORS_PER_BLOCK (4096 / ATA_SECTOR_SIZE)  /* 8 sectors = 1 block */

/* Timeout for polling (iterations) */
#define ATA_TIMEOUT         100000

/* ========== ATA Device Info ========== */

/**
 * @brief Information about a detected ATA drive
 */
struct ata_device {
    uint16_t io_base;           /* Base I/O port (0x1F0 for primary)    */
    uint16_t ctrl_base;         /* Control port (0x3F6 for primary)     */
    uint8_t  drive_select;      /* ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE  */
    int      present;           /* Drive detected?                      */
    uint64_t total_sectors;     /* Total LBA28 sectors (max ~128 GB)    */
    char     model[41];         /* Model string from IDENTIFY (NUL-term)*/
};

/* ========== ATA Driver API ========== */

/**
 * @brief Initialize the ATA PIO subsystem
 *
 * Probes primary master and slave for attached drives.
 * Does NOT register them as block devices (call ata_get_block_device for that).
 */
void ata_init(void);

/**
 * @brief Get an ATA drive as a block_device
 *
 * Wraps the detected ATA drive in a block_device struct with
 * proper ops for the block layer. Caller should register it with
 * block_device_register().
 *
 * @param drive 0 = primary master, 1 = primary slave
 * @return Pointer to block_device on success, NULL if drive not present
 */
struct block_device *ata_get_block_device(int drive);

/**
 * @brief Read one 512-byte sector via ATA PIO
 * @param dev ATA device
 * @param lba  LBA sector number
 * @param buf  Destination buffer (must be >= 512 bytes)
 * @return 0 on success, negative on error
 */
int ata_read_sector(struct ata_device *dev, uint32_t lba, void *buf);

/**
 * @brief Write one 512-byte sector via ATA PIO
 * @param dev ATA device
 * @param lba  LBA sector number
 * @param buf  Source buffer (must be >= 512 bytes)
 * @return 0 on success, negative on error
 */
int ata_write_sector(struct ata_device *dev, uint32_t lba, const void *buf);

#endif /* ATA_H */
