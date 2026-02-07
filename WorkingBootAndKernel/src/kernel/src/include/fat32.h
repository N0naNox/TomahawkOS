/**
 * @file fat32.h
 * @brief FAT32 On-Disk Structures and Constants
 *
 * All structures are packed to match the exact on-disk byte layout.
 * Reference: Microsoft FAT32 File System Specification (fatgen103.doc)
 */

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct block_device;
struct vnode;

/* ========== Disk Geometry Constants ========== */

#define FAT32_SECTOR_SIZE       512     /* FAT32 always uses 512-byte sectors */
#define FAT32_DIRENT_SIZE       32      /* Each directory entry is 32 bytes    */
#define FAT32_LFN_CHARS_PER     13      /* Characters stored per LFN entry     */
#define FAT32_MAX_FILENAME      255     /* Maximum LFN length                  */
#define FAT32_SHORT_NAME_LEN    11      /* 8.3 name stored as 8+3 no dot      */

/* ========== FAT Entry Special Values ========== */

#define FAT32_CLUSTER_FREE      0x00000000  /* Cluster is free                */
#define FAT32_CLUSTER_RESERVED  0x00000001  /* Reserved                       */
#define FAT32_CLUSTER_BAD       0x0FFFFFF7  /* Bad cluster mark               */
#define FAT32_CLUSTER_EOC_MIN   0x0FFFFFF8  /* End-of-chain minimum           */
#define FAT32_CLUSTER_EOC       0x0FFFFFFF  /* End-of-chain (typical)         */
#define FAT32_CLUSTER_MASK      0x0FFFFFFF  /* Mask for 28-bit cluster number */

/* First valid data cluster is always 2 */
#define FAT32_FIRST_DATA_CLUSTER 2

/* ========== Directory Entry Attributes ========== */

#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20

/* LFN entries have all four lower attribute bits set */
#define FAT32_ATTR_LFN          (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | \
                                 FAT32_ATTR_SYSTEM    | FAT32_ATTR_VOLUME_ID)

/* Mask to test if entry is an LFN slot */
#define FAT32_ATTR_LFN_MASK     (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | \
                                 FAT32_ATTR_SYSTEM    | FAT32_ATTR_VOLUME_ID | \
                                 FAT32_ATTR_DIRECTORY  | FAT32_ATTR_ARCHIVE)

/* ========== Directory Entry Sentinel Values ========== */

#define FAT32_DIRENT_FREE       0xE5    /* Entry is free (deleted)            */
#define FAT32_DIRENT_END        0x00    /* Entry is free AND no more after it */
#define FAT32_DIRENT_KANJI      0x05    /* Actual first byte is 0xE5 (Kanji) */

/* ========== LFN Sequence Number ========== */

#define FAT32_LFN_SEQ_MASK     0x1F    /* Bits holding the sequence number   */
#define FAT32_LFN_LAST_ENTRY   0x40    /* OR'd into seq for the last LFN slot*/

/* ========== Boot Signature ========== */

#define FAT32_BOOT_SIGNATURE    0xAA55
#define FAT32_FSINFO_LEAD_SIG   0x41615252
#define FAT32_FSINFO_STRUC_SIG  0x61417272
#define FAT32_FSINFO_TRAIL_SIG  0xAA550000

/* ========== On-Disk Structures (packed, byte-exact layout) ========== */

/**
 * @brief FAT32 BIOS Parameter Block (BPB) + Boot Sector
 *
 * Occupies the first 512 bytes of the volume (sector 0).
 * Fields up to BS_VolLab match the Microsoft spec exactly.
 */
struct fat32_bpb {
    /* Common BPB (offsets 0-35) */
    uint8_t  BS_JmpBoot[3];        /*  0: Jump instruction to boot code      */
    uint8_t  BS_OEMName[8];        /*  3: OEM name / formatting tool         */
    uint16_t BPB_BytsPerSec;       /* 11: Bytes per sector (usually 512)     */
    uint8_t  BPB_SecPerClus;       /* 13: Sectors per cluster (power of 2)   */
    uint16_t BPB_RsvdSecCnt;       /* 14: Reserved sectors before first FAT  */
    uint8_t  BPB_NumFATs;          /* 16: Number of FATs (usually 2)         */
    uint16_t BPB_RootEntCnt;       /* 17: Root entry count (0 for FAT32)     */
    uint16_t BPB_TotSec16;        /* 19: Total sectors 16-bit (0 for FAT32) */
    uint8_t  BPB_Media;            /* 21: Media type (0xF8 = hard disk)      */
    uint16_t BPB_FATSz16;         /* 22: FAT size 16-bit (0 for FAT32)      */
    uint16_t BPB_SecPerTrk;       /* 24: Sectors per track (CHS geometry)   */
    uint16_t BPB_NumHeads;        /* 26: Number of heads (CHS geometry)     */
    uint32_t BPB_HiddSec;         /* 28: Hidden sectors before partition    */
    uint32_t BPB_TotSec32;        /* 32: Total sectors 32-bit               */

    /* FAT32-specific fields (offsets 36-89) */
    uint32_t BPB_FATSz32;         /* 36: Sectors per FAT (32-bit)           */
    uint16_t BPB_ExtFlags;        /* 40: FAT mirroring flags                */
    uint16_t BPB_FSVer;           /* 42: Filesystem version (0.0)           */
    uint32_t BPB_RootClus;        /* 44: First cluster of root directory    */
    uint16_t BPB_FSInfo;          /* 48: Sector number of FSInfo            */
    uint16_t BPB_BkBootSec;       /* 50: Sector of backup boot sector      */
    uint8_t  BPB_Reserved[12];    /* 52: Reserved, must be zero             */

    /* Extended Boot Record */
    uint8_t  BS_DrvNum;           /* 64: Drive number (0x80 for HDD)        */
    uint8_t  BS_Reserved1;        /* 65: Reserved                           */
    uint8_t  BS_BootSig;          /* 66: Extended boot signature (0x29)     */
    uint32_t BS_VolID;            /* 67: Volume serial number               */
    uint8_t  BS_VolLab[11];       /* 71: Volume label (padded with spaces)  */
    uint8_t  BS_FilSysType[8];    /* 82: "FAT32   " (informational only)   */
} __attribute__((packed));

/**
 * @brief FAT32 FSInfo Sector
 *
 * Usually at sector 1. Provides hints about free cluster count
 * and the next free cluster for faster allocation.
 */
struct fat32_fsinfo {
    uint32_t FSI_LeadSig;          /*   0: 0x41615252 */
    uint8_t  FSI_Reserved1[480];   /*   4: Reserved   */
    uint32_t FSI_StrucSig;         /* 484: 0x61417272 */
    uint32_t FSI_Free_Count;       /* 488: Free cluster count (0xFFFFFFFF = unknown) */
    uint32_t FSI_Nxt_Free;         /* 492: Hint for next free cluster       */
    uint8_t  FSI_Reserved2[12];    /* 496: Reserved   */
    uint32_t FSI_TrailSig;         /* 508: 0xAA550000 */
} __attribute__((packed));

/**
 * @brief FAT32 Short Directory Entry (8.3 name)
 *
 * 32 bytes. Every file/directory/volume-label on disk uses this format.
 * Long names are stored in preceding LFN entries.
 */
struct fat32_dirent {
    uint8_t  DIR_Name[11];         /*  0: Short name (8 name + 3 ext, space-padded) */
    uint8_t  DIR_Attr;             /* 11: File attributes                    */
    uint8_t  DIR_NTRes;            /* 12: Reserved for Windows NT (case info)*/
    uint8_t  DIR_CrtTimeTenth;     /* 13: Creation time tenths of a second   */
    uint16_t DIR_CrtTime;          /* 14: Creation time (HMS packed)         */
    uint16_t DIR_CrtDate;          /* 16: Creation date (YMD packed)         */
    uint16_t DIR_LstAccDate;       /* 18: Last access date                   */
    uint16_t DIR_FstClusHI;        /* 20: High 16 bits of first cluster      */
    uint16_t DIR_WrtTime;          /* 22: Last write time                    */
    uint16_t DIR_WrtDate;          /* 24: Last write date                    */
    uint16_t DIR_FstClusLO;        /* 26: Low 16 bits of first cluster       */
    uint32_t DIR_FileSize;         /* 28: File size in bytes (0 for dirs)    */
} __attribute__((packed));

/**
 * @brief FAT32 Long File Name (LFN) Directory Entry
 *
 * 32 bytes, same size as a short entry but different layout.
 * Identified by DIR_Attr == FAT32_ATTR_LFN.
 * Stored in reverse order (last chunk first) before the short entry.
 * Characters are UCS-2 (little-endian 16-bit).
 */
struct fat32_lfn_entry {
    uint8_t  LDIR_Ord;             /*  0: Sequence number (1-based, 0x40 = last) */
    uint16_t LDIR_Name1[5];        /*  1: Characters 1-5 (UCS-2)            */
    uint8_t  LDIR_Attr;            /* 11: Must be FAT32_ATTR_LFN (0x0F)     */
    uint8_t  LDIR_Type;            /* 12: 0 for LFN entries                  */
    uint8_t  LDIR_Chksum;          /* 13: Checksum of associated short name  */
    uint16_t LDIR_Name2[6];        /* 14: Characters 6-11 (UCS-2)           */
    uint16_t LDIR_FstClusLO;       /* 26: Must be 0                          */
    uint16_t LDIR_Name3[2];        /* 28: Characters 12-13 (UCS-2)          */
} __attribute__((packed));

/* ========== Compile-Time Sanity Checks ========== */

_Static_assert(sizeof(struct fat32_bpb)       == 90,
               "fat32_bpb must be exactly 90 bytes");
_Static_assert(sizeof(struct fat32_fsinfo)    == 512,
               "fat32_fsinfo must be exactly 512 bytes");
_Static_assert(sizeof(struct fat32_dirent)    == 32,
               "fat32_dirent must be exactly 32 bytes");
_Static_assert(sizeof(struct fat32_lfn_entry) == 32,
               "fat32_lfn_entry must be exactly 32 bytes");

/* ========== Runtime Mount State ========== */

/**
 * @brief Per-mount FAT32 state
 *
 * Computed once at mount time from the BPB; used by all
 * subsequent FAT32 operations.
 */
struct fat32_mount {
    struct block_device *dev;       /* Underlying block device               */

    /* Geometry (copied / computed from BPB) */
    uint16_t bytes_per_sec;         /* Almost always 512                     */
    uint8_t  sec_per_clus;          /* Sectors per cluster                   */
    uint32_t cluster_size;          /* bytes_per_sec * sec_per_clus          */

    /* FAT region */
    uint32_t fat_start_sector;      /* First sector of first FAT             */
    uint32_t fat_size_sectors;      /* Sectors per FAT                       */
    uint8_t  num_fats;              /* Number of FATs (usually 2)            */

    /* Data region */
    uint32_t data_start_sector;     /* First sector of the data region       */
    uint32_t total_data_clusters;   /* Total usable data clusters            */
    uint32_t root_cluster;          /* First cluster of root directory       */

    /* FSInfo hints (updated on alloc/free) */
    uint32_t fsinfo_sector;         /* Sector number of FSInfo               */
    uint32_t free_count;            /* Cached free cluster count             */
    uint32_t next_free;             /* Hint: next free cluster to try        */

    /* Volume label (space-padded, not null-terminated from disk) */
    char volume_label[12];          /* 11 chars + NUL                        */
};

/* ========== Helper Macros ========== */

/**
 * Convert a cluster number to its first sector on disk.
 * Cluster 2 is the first data cluster.
 */
#define FAT32_CLUSTER_TO_SECTOR(mount, cluster) \
    ((mount)->data_start_sector + \
     (((uint64_t)(cluster) - FAT32_FIRST_DATA_CLUSTER) * (mount)->sec_per_clus))

/**
 * Convert a cluster number to a byte offset on the block device.
 */
#define FAT32_CLUSTER_TO_OFFSET(mount, cluster) \
    ((uint64_t)FAT32_CLUSTER_TO_SECTOR(mount, cluster) * (mount)->bytes_per_sec)

/**
 * Get the sector containing a given FAT entry (for cluster number).
 */
#define FAT32_FAT_SECTOR_FOR_CLUSTER(mount, cluster) \
    ((mount)->fat_start_sector + \
     (((cluster) * 4) / (mount)->bytes_per_sec))

/**
 * Get the byte offset within a FAT sector for a given cluster entry.
 */
#define FAT32_FAT_OFFSET_IN_SECTOR(mount, cluster) \
    (((cluster) * 4) % (mount)->bytes_per_sec)

/**
 * Combine high and low 16-bit cluster fields from a directory entry.
 */
#define FAT32_DIRENT_CLUSTER(dirent) \
    (((uint32_t)(dirent)->DIR_FstClusHI << 16) | (uint32_t)(dirent)->DIR_FstClusLO)

/**
 * Check if a FAT entry value marks end-of-chain.
 */
#define FAT32_IS_EOC(val) \
    (((val) & FAT32_CLUSTER_MASK) >= FAT32_CLUSTER_EOC_MIN)

/**
 * Check if a directory entry is an LFN slot.
 */
#define FAT32_IS_LFN(dirent) \
    (((dirent)->DIR_Attr & FAT32_ATTR_LFN_MASK) == FAT32_ATTR_LFN)

/**
 * Check if a directory entry marks end-of-directory.
 */
#define FAT32_IS_DIRENT_END(dirent) \
    ((dirent)->DIR_Name[0] == FAT32_DIRENT_END)

/**
 * Check if a directory entry is free (deleted).
 */
#define FAT32_IS_DIRENT_FREE(dirent) \
    ((dirent)->DIR_Name[0] == FAT32_DIRENT_FREE)

/**
 * Compute the 8.3 short name checksum used by LFN entries.
 */
static inline uint8_t fat32_lfn_checksum(const uint8_t *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < FAT32_SHORT_NAME_LEN; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    return sum;
}

/* ========== FAT32 Driver API (implemented in fat32.c) ========== */

/**
 * @brief Register the FAT32 filesystem type with the mount subsystem
 * Call once during boot (from kernel_main or fs_init_root).
 */
void fat32_register(void);

/**
 * @brief Mount a FAT32 volume
 * @param dev  Block device containing the FAT32 filesystem
 * @param flags Mount flags
 * @return Root vnode of the mounted filesystem, or NULL on error
 */
struct vnode *fat32_mount(struct block_device *dev, int flags);

/**
 * @brief Unmount a FAT32 volume
 * @param root Root vnode returned by fat32_mount
 * @return 0 on success, negative on error
 */
int fat32_unmount(struct vnode *root);

/**
 * @brief Sync FAT32 metadata (FATs, FSInfo) to disk
 * @param root Root vnode
 * @return 0 on success, negative on error
 */
int fat32_sync(struct vnode *root);

#endif /* FAT32_H */
