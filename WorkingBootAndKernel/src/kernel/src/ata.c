/**
 * @file ata.c
 * @brief ATA PIO Disk Driver Implementation
 *
 * Minimal ATA PIO-mode driver for x86-64.
 * - Probes primary bus master/slave via IDENTIFY
 * - Reads/writes 512-byte sectors using LBA28 addressing
 * - Implements block_device_ops (4K blocks = 8 sectors each)
 *
 * Designed for QEMU's emulated IDE controller (`-hdb disk.img`).
 */

#include "include/ata.h"
#include "include/block_device.h"
#include "include/hal_port_io.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include <uart.h>

/* ========== Internal State ========== */

/* We support primary master (0) and primary slave (1) */
#define ATA_MAX_DRIVES 2
static struct ata_device ata_drives[ATA_MAX_DRIVES];

/* Pre-allocated block_device wrappers */
static struct block_device ata_blkdev[ATA_MAX_DRIVES];
static int ata_initialized = 0;

/* ========== Low-Level Helpers ========== */

/**
 * Small delay by reading the alt-status port 4 times (~400ns).
 * Required after writing to command/drive registers.
 */
static void ata_io_delay(uint16_t ctrl_port) {
    hal_inb(ctrl_port);
    hal_inb(ctrl_port);
    hal_inb(ctrl_port);
    hal_inb(ctrl_port);
}

/**
 * Poll BSY until clear, then check DRQ or error.
 * Returns 0 on success (DRQ set), negative on error/timeout.
 */
static int ata_poll(uint16_t io_base, uint16_t ctrl_port) {
    /* Required: read alt-status 4 times after command write for ~400ns delay.
     * This prevents reading stale status from the primary register. */
    ata_io_delay(ctrl_port);

    /* Wait for BSY to clear */
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = hal_inb(io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            /* Check for errors */
            if (status & ATA_SR_ERR) {
                return -1;
            }
            if (status & ATA_SR_DF) {
                return -2;
            }
            /* Check DRQ */
            if (status & ATA_SR_DRQ) {
                return 0;
            }
        }
    }
    return -3; /* Timeout */
}

/**
 * Wait for BSY to clear (no DRQ check).
 */
static int ata_wait_ready(uint16_t io_base) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = hal_inb(io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

/* ========== IDENTIFY Device ========== */

/**
 * Run the IDENTIFY command on a drive to detect its presence and capacity.
 */
static int ata_identify(struct ata_device *dev) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    /* Select drive */
    hal_outb(io + ATA_REG_DRIVE, dev->drive_select);
    ata_io_delay(ctrl);

    /* Zero out sector count and LBA registers */
    hal_outb(io + ATA_REG_SECCOUNT, 0);
    hal_outb(io + ATA_REG_LBA_LO, 0);
    hal_outb(io + ATA_REG_LBA_MID, 0);
    hal_outb(io + ATA_REG_LBA_HI, 0);

    /* Send IDENTIFY command */
    hal_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_delay(ctrl);

    /* Read status — if 0, no drive */
    uint8_t status = hal_inb(io + ATA_REG_STATUS);
    if (status == 0) {
        return -1; /* No drive */
    }

    /* Wait for BSY to clear */
    int timeout = ATA_TIMEOUT;
    while ((status & ATA_SR_BSY) && timeout > 0) {
        status = hal_inb(io + ATA_REG_STATUS);
        timeout--;
    }
    if (timeout == 0) {
        return -2; /* Timeout */
    }

    /* Check for ATAPI/SATA (LBA mid/hi become non-zero) */
    uint8_t lba_mid = hal_inb(io + ATA_REG_LBA_MID);
    uint8_t lba_hi  = hal_inb(io + ATA_REG_LBA_HI);
    if (lba_mid != 0 || lba_hi != 0) {
        return -3; /* Not ATA (maybe ATAPI) */
    }

    /* Poll until DRQ or error */
    if (ata_poll(io, ctrl) != 0) {
        return -4;
    }

    /* Read 256 words of identification data */
    uint16_t identify_buf[256];
    hal_insw(io + ATA_REG_DATA, identify_buf, 256);

    /* Extract total LBA28 sectors from words 60-61 */
    dev->total_sectors = (uint32_t)identify_buf[60] |
                         ((uint32_t)identify_buf[61] << 16);

    /* Extract model string from words 27-46 (40 chars, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        uint16_t w = identify_buf[27 + i];
        dev->model[i * 2]     = (char)(w >> 8);
        dev->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    dev->model[40] = '\0';

    /* Trim trailing spaces from model string */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }

    dev->present = 1;
    return 0;
}

/* ========== Sector-Level I/O ========== */

int ata_read_sector(struct ata_device *dev, uint32_t lba, void *buf) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    if (ata_wait_ready(io) != 0) {
        return -1;
    }

    /* Select drive + top 4 bits of LBA28 */
    hal_outb(io + ATA_REG_DRIVE,
             dev->drive_select | ((lba >> 24) & 0x0F));
    ata_io_delay(ctrl);

    /* Set sector count = 1, LBA bits 0-23 */
    hal_outb(io + ATA_REG_SECCOUNT, 1);
    hal_outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    hal_outb(io + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    hal_outb(io + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Issue READ SECTORS command */
    hal_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    /* Wait for data */
    if (ata_poll(io, ctrl) != 0) {
        return -2;
    }

    /* Read 256 words (512 bytes) */
    hal_insw(io + ATA_REG_DATA, buf, 256);

    return 0;
}

int ata_write_sector(struct ata_device *dev, uint32_t lba, const void *buf) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    if (ata_wait_ready(io) != 0) {
        return -1;
    }

    /* Select drive + top 4 bits of LBA28 */
    hal_outb(io + ATA_REG_DRIVE,
             dev->drive_select | ((lba >> 24) & 0x0F));
    ata_io_delay(ctrl);

    /* Set sector count = 1, LBA bits 0-23 */
    hal_outb(io + ATA_REG_SECCOUNT, 1);
    hal_outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    hal_outb(io + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    hal_outb(io + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Issue WRITE SECTORS command */
    hal_outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    /* Wait for DRQ */
    if (ata_poll(io, ctrl) != 0) {
        return -2;
    }

    /* Write 256 words (512 bytes) */
    hal_outsw(io + ATA_REG_DATA, buf, 256);

    /* Flush the write cache */
    hal_outb(io + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_ready(io) != 0) {
        return -3;
    }

    return 0;
}

/* ========== block_device_ops Implementation ========== */

/*
 * The block layer uses BLOCK_SIZE (4096) byte blocks.
 * Each block = 8 ATA sectors of 512 bytes.
 */

static int ata_blk_read(struct block_device *dev, uint64_t block_num, void *buffer) {
    struct ata_device *ata = (struct ata_device *)dev->private_data;
    uint32_t start_lba = (uint32_t)(block_num * ATA_SECTORS_PER_BLOCK);
    uint8_t *dst = (uint8_t *)buffer;

    for (int i = 0; i < ATA_SECTORS_PER_BLOCK; i++) {
        int rc = -1;
        for (int retry = 0; retry < 3 && rc != 0; retry++) {
            rc = ata_read_sector(ata, start_lba + i, dst);
        }
        if (rc != 0) {
            uart_puts("[ATA] read error at LBA ");
            uart_putu(start_lba + i);
            uart_puts(" after 3 retries\n");
            return -1;
        }
        dst += ATA_SECTOR_SIZE;
    }
    return 0;
}

static int ata_blk_write(struct block_device *dev, uint64_t block_num, const void *buffer) {
    struct ata_device *ata = (struct ata_device *)dev->private_data;
    uint32_t start_lba = (uint32_t)(block_num * ATA_SECTORS_PER_BLOCK);
    const uint8_t *src = (const uint8_t *)buffer;

    for (int i = 0; i < ATA_SECTORS_PER_BLOCK; i++) {
        int rc = -1;
        for (int retry = 0; retry < 3 && rc != 0; retry++) {
            rc = ata_write_sector(ata, start_lba + i, src);
        }
        if (rc != 0) {
            uart_puts("[ATA] write error at LBA ");
            uart_putu(start_lba + i);
            uart_puts(" after 3 retries\n");
            return -1;
        }
        src += ATA_SECTOR_SIZE;
    }
    return 0;
}

static int ata_blk_sync(struct block_device *dev) {
    struct ata_device *ata = (struct ata_device *)dev->private_data;
    hal_outb(ata->io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    return ata_wait_ready(ata->io_base);
}

static uint64_t ata_blk_get_size(struct block_device *dev) {
    return dev->total_blocks;
}

static struct block_device_ops ata_block_ops = {
    .read_block  = ata_blk_read,
    .write_block = ata_blk_write,
    .sync        = ata_blk_sync,
    .get_size    = ata_blk_get_size,
};

/* ========== Public API ========== */

void ata_init(void) {
    if (ata_initialized) return;

    uart_puts("[ATA] Probing primary bus...\n");

    /* Disable interrupts on primary controller */
    hal_outb(ATA_PRIMARY_CTRL, ATA_CTRL_NIEN);

    /* Probe master */
    memset(&ata_drives[0], 0, sizeof(struct ata_device));
    ata_drives[0].io_base      = ATA_PRIMARY_IO;
    ata_drives[0].ctrl_base    = ATA_PRIMARY_CTRL;
    ata_drives[0].drive_select = ATA_DRIVE_MASTER;

    if (ata_identify(&ata_drives[0]) == 0) {
        uart_puts("[ATA] Primary master: ");
        uart_puts(ata_drives[0].model);
        uart_puts(" (");
        uart_putu(ata_drives[0].total_sectors);
        uart_puts(" sectors, ");
        uart_putu((ata_drives[0].total_sectors * ATA_SECTOR_SIZE) / (1024 * 1024));
        uart_puts(" MB)\n");
    } else {
        uart_puts("[ATA] Primary master: not present\n");
    }

    /* Probe slave */
    memset(&ata_drives[1], 0, sizeof(struct ata_device));
    ata_drives[1].io_base      = ATA_PRIMARY_IO;
    ata_drives[1].ctrl_base    = ATA_PRIMARY_CTRL;
    ata_drives[1].drive_select = ATA_DRIVE_SLAVE;

    if (ata_identify(&ata_drives[1]) == 0) {
        uart_puts("[ATA] Primary slave: ");
        uart_puts(ata_drives[1].model);
        uart_puts(" (");
        uart_putu(ata_drives[1].total_sectors);
        uart_puts(" sectors, ");
        uart_putu((ata_drives[1].total_sectors * ATA_SECTOR_SIZE) / (1024 * 1024));
        uart_puts(" MB)\n");
    } else {
        uart_puts("[ATA] Primary slave: not present\n");
    }

    ata_initialized = 1;
    uart_puts("[ATA] Init done\n");
}

struct block_device *ata_get_block_device(int drive) {
    if (drive < 0 || drive >= ATA_MAX_DRIVES) {
        return NULL;
    }
    if (!ata_drives[drive].present) {
        return NULL;
    }

    struct block_device *dev = &ata_blkdev[drive];
    memset(dev, 0, sizeof(*dev));

    /* Name: "hda" for master, "hdb" for slave */
    dev->name[0] = 'h';
    dev->name[1] = 'd';
    dev->name[2] = 'a' + (char)drive;
    dev->name[3] = '\0';

    dev->block_size   = BLOCK_SIZE;  /* 4096 */
    dev->total_blocks = ata_drives[drive].total_sectors / ATA_SECTORS_PER_BLOCK;
    dev->ops          = &ata_block_ops;
    dev->private_data = &ata_drives[drive];
    dev->flags        = 0;
    dev->ref_count    = 0;

    return dev;
}
