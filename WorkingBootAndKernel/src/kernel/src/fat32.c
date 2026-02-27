/**
 * @file fat32.c
 * @brief FAT32 Filesystem Driver
 *
 * Implements:
 * - BPB parsing and mount/unmount
 * - FAT table read/write helpers (cluster chain traversal, allocation)
 * - Directory reading (short 8.3 + LFN)
 * - File read/write
 * - Filesystem registration for the mount subsystem
 *
 * Uses the block device layer's block_read/block_write for all disk I/O,
 * which handles 512-byte sector alignment via byte offsets internally.
 */

#include "include/fat32.h"
#include "include/block_device.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/mount.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include <uart.h>
#include <stddef.h>

/* ========== Per-Vnode FAT32 Data ========== */

/**
 * Attached to every FAT32 vnode via v_data.
 * Stores the file/directory's cluster and size info.
 */
struct fat32_vnode_data {
    uint32_t start_cluster;     /* First cluster of file/dir            */
    uint32_t file_size;         /* Size in bytes (0 for directories)    */
    uint8_t  attr;              /* FAT32 attribute byte                 */
    uint32_t dir_cluster;       /* Parent directory's cluster           */
    uint32_t dir_entry_offset;  /* Byte offset of this entry in parent  */
};

/* Forward declarations for vnode ops */
static int fat32_vop_open(struct vnode *vp);
static int fat32_vop_close(struct vnode *vp);
static int fat32_vop_read_at(struct vnode *vp, void *buf, size_t nbyte, uint64_t offset);
static int fat32_vop_write_at(struct vnode *vp, const void *buf, size_t nbyte, uint64_t offset);
static int fat32_vop_lookup(struct vnode *dir, const char *name, struct vnode **result);
static int fat32_vop_readdir(struct vnode *dir, struct vfs_dirent *dent, uint64_t *offset);
static int fat32_vop_create(struct vnode *dir, const char *name, struct vnode **result);
static int fat32_vop_mkdir(struct vnode *dir, const char *name, struct vnode **result);
static int fat32_vop_remove(struct vnode *dir, const char *name);
static int64_t fat32_vop_getsize(struct vnode *vp);

/* FAT32 vnode operations table */
static struct vnode_ops fat32_vnode_ops = {
    .vop_open     = fat32_vop_open,
    .vop_close    = fat32_vop_close,
    .vop_read     = NULL,           /* Use vop_read_at instead */
    .vop_write    = NULL,           /* Use vop_write_at instead */
    .vop_read_at  = fat32_vop_read_at,
    .vop_write_at = fat32_vop_write_at,
    .vop_lookup   = fat32_vop_lookup,
    .vop_readdir  = fat32_vop_readdir,
    .vop_create   = fat32_vop_create,
    .vop_mkdir    = fat32_vop_mkdir,
    .vop_remove   = fat32_vop_remove,
    .vop_getsize  = fat32_vop_getsize,
};

/* ========== Sector I/O Helpers ========== */

/**
 * Read 'count' bytes at a raw byte offset from the block device.
 * Wraps block_read which handles cross-block and sub-block reads.
 */
static int fat32_read_bytes(struct fat32_mount *mnt, uint64_t byte_offset,
                            void *buf, size_t count) {
    ssize_t ret = block_read(mnt->dev, byte_offset, buf, count);
    return (ret == (ssize_t)count) ? 0 : -1;
}

/**
 * Write 'count' bytes at a raw byte offset on the block device.
 */
static int fat32_write_bytes(struct fat32_mount *mnt, uint64_t byte_offset,
                             const void *buf, size_t count) {
    ssize_t ret = block_write(mnt->dev, byte_offset, buf, count);
    return (ret == (ssize_t)count) ? 0 : -1;
}

/**
 * Read one 512-byte sector into buf.
 */
static int fat32_read_sector(struct fat32_mount *mnt, uint32_t sector, void *buf) {
    return fat32_read_bytes(mnt, (uint64_t)sector * FAT32_SECTOR_SIZE,
                            buf, FAT32_SECTOR_SIZE);
}

/* ========== FAT Table Helpers (Task 5) ========== */

/**
 * Read the FAT entry for a given cluster number.
 * Returns the 28-bit next-cluster value (masked), or 0 on error.
 */
uint32_t fat32_fat_read(struct fat32_mount *mnt, uint32_t cluster) {
    /* Byte offset of this cluster's entry in the FAT */
    uint64_t fat_offset = (uint64_t)mnt->fat_start_sector * mnt->bytes_per_sec
                        + (uint64_t)cluster * 4;

    uint32_t entry = 0;
    if (fat32_read_bytes(mnt, fat_offset, &entry, 4) != 0) {
        uart_puts("[FAT32] Error reading FAT entry for cluster ");
        uart_putu(cluster);
        uart_puts("\n");
        return 0;
    }
    return entry & FAT32_CLUSTER_MASK;
}

/**
 * Write a FAT entry for a given cluster number.
 * Preserves the top 4 reserved bits of the existing entry.
 * If num_fats > 1, mirrors the write to all FAT copies.
 */
int fat32_fat_write(struct fat32_mount *mnt, uint32_t cluster, uint32_t value) {
    for (uint8_t fat_idx = 0; fat_idx < mnt->num_fats; fat_idx++) {
        uint64_t fat_offset = ((uint64_t)mnt->fat_start_sector
                              + (uint64_t)fat_idx * mnt->fat_size_sectors)
                              * mnt->bytes_per_sec
                              + (uint64_t)cluster * 4;

        /* Read existing entry to preserve top 4 bits */
        uint32_t existing = 0;
        if (fat32_read_bytes(mnt, fat_offset, &existing, 4) != 0) {
            return -1;
        }

        uint32_t new_val = (existing & 0xF0000000) | (value & FAT32_CLUSTER_MASK);
        if (fat32_write_bytes(mnt, fat_offset, &new_val, 4) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Follow a cluster chain for 'n' links.
 * Returns the cluster number reached after n steps, or 0 on error/EOC.
 */
uint32_t fat32_follow_chain(struct fat32_mount *mnt, uint32_t start, uint32_t n) {
    uint32_t cur = start;
    for (uint32_t i = 0; i < n; i++) {
        cur = fat32_fat_read(mnt, cur);
        if (cur == 0 || FAT32_IS_EOC(cur) || cur == FAT32_CLUSTER_BAD) {
            return 0;  /* Ran off the end or hit bad cluster */
        }
    }
    return cur;
}

/**
 * Count the number of clusters in a chain starting at 'start'.
 */
uint32_t fat32_chain_length(struct fat32_mount *mnt, uint32_t start) {
    uint32_t count = 0;
    uint32_t cur = start;
    while (cur >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(cur)
           && cur != FAT32_CLUSTER_BAD) {
        count++;
        cur = fat32_fat_read(mnt, cur);
        if (cur == 0) break;  /* Read error */
        /* Safety: prevent infinite loops on corrupt FATs */
        if (count > mnt->total_data_clusters) {
            uart_puts("[FAT32] WARNING: cluster chain loop detected\n");
            break;
        }
    }
    return count;
}

/**
 * Allocate a single free cluster from the FAT.
 * Marks it as EOC and updates FSInfo hints.
 * Returns the cluster number, or 0 on failure.
 */
uint32_t fat32_alloc_cluster(struct fat32_mount *mnt) {
    uint32_t total = mnt->total_data_clusters + FAT32_FIRST_DATA_CLUSTER;
    uint32_t start = (mnt->next_free >= FAT32_FIRST_DATA_CLUSTER)
                   ? mnt->next_free : FAT32_FIRST_DATA_CLUSTER;

    /* Scan from hint, wrapping around */
    for (uint32_t i = 0; i < mnt->total_data_clusters; i++) {
        uint32_t candidate = start + i;
        if (candidate >= total) {
            candidate = FAT32_FIRST_DATA_CLUSTER + (candidate - total);
        }

        uint32_t entry = fat32_fat_read(mnt, candidate);
        if (entry == FAT32_CLUSTER_FREE) {
            /* Mark as end-of-chain */
            if (fat32_fat_write(mnt, candidate, FAT32_CLUSTER_EOC) != 0) {
                return 0;
            }

            /* Update hints */
            mnt->next_free = candidate + 1;
            if (mnt->free_count != 0xFFFFFFFF && mnt->free_count > 0) {
                mnt->free_count--;
            }
            return candidate;
        }
    }

    uart_puts("[FAT32] ERROR: No free clusters\n");
    return 0;
}

/**
 * Extend a cluster chain by one cluster.
 * Appends a newly allocated cluster after 'last_cluster'.
 * Returns the new cluster number, or 0 on failure.
 */
uint32_t fat32_extend_chain(struct fat32_mount *mnt, uint32_t last_cluster) {
    uint32_t new_clus = fat32_alloc_cluster(mnt);
    if (new_clus == 0) return 0;

    /* Link the old last cluster to the new one */
    if (fat32_fat_write(mnt, last_cluster, new_clus) != 0) {
        /* Roll back allocation */
        fat32_fat_write(mnt, new_clus, FAT32_CLUSTER_FREE);
        return 0;
    }
    return new_clus;
}

/**
 * Free an entire cluster chain starting at 'start'.
 */
int fat32_free_chain(struct fat32_mount *mnt, uint32_t start) {
    uint32_t cur = start;
    uint32_t safety = 0;

    while (cur >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(cur)
           && cur != FAT32_CLUSTER_BAD) {
        uint32_t next = fat32_fat_read(mnt, cur);
        if (fat32_fat_write(mnt, cur, FAT32_CLUSTER_FREE) != 0) {
            return -1;
        }
        if (mnt->free_count != 0xFFFFFFFF) {
            mnt->free_count++;
        }
        cur = next;
        if (++safety > mnt->total_data_clusters) {
            uart_puts("[FAT32] WARNING: loop in free_chain\n");
            break;
        }
    }
    return 0;
}

/**
 * Read the data of a cluster into buf.
 * buf must be at least cluster_size bytes.
 */
int fat32_read_cluster(struct fat32_mount *mnt, uint32_t cluster, void *buf) {
    if (cluster < FAT32_FIRST_DATA_CLUSTER) return -1;
    uint64_t offset = FAT32_CLUSTER_TO_OFFSET(mnt, cluster);
    uart_puts("[FAT32] read_cluster: cluster=");
    uart_putu(cluster);
    uart_puts(" byte_offset=0x");
    uart_puthex(offset);
    uart_puts(" size=");
    uart_putu(mnt->cluster_size);
    uart_puts("\n");
    int rc = fat32_read_bytes(mnt, offset, buf, mnt->cluster_size);
    if (rc != 0) {
        uart_puts("[FAT32] read_cluster: FAILED rc=");
        uart_putu((uint64_t)(int64_t)rc);
        uart_puts("\n");
    }
    return rc;
}

/**
 * Write data to a cluster from buf.
 * buf must be at least cluster_size bytes.
 */
int fat32_write_cluster(struct fat32_mount *mnt, uint32_t cluster, const void *buf) {
    if (cluster < FAT32_FIRST_DATA_CLUSTER) return -1;
    uint64_t offset = FAT32_CLUSTER_TO_OFFSET(mnt, cluster);
    return fat32_write_bytes(mnt, offset, buf, mnt->cluster_size);
}

/* ========== Vnode Creation Helper ========== */

/**
 * Create a FAT32 vnode for a file or directory.
 */
static struct vnode *fat32_create_vnode(struct fat32_mount *mnt,
                                        uint32_t cluster, uint32_t size,
                                        uint8_t attr) {
    /* Allocate vnode */
    struct vnode *vp = (struct vnode *)pfa_alloc_frame();
    if (!vp) return NULL;

    /* Allocate per-vnode data */
    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)pfa_alloc_frame();
    if (!fvd) {
        pfa_free_frame((uintptr_t)vp);
        return NULL;
    }

    memset(vp, 0, sizeof(*vp));
    memset(fvd, 0, sizeof(*fvd));

    fvd->start_cluster = cluster;
    fvd->file_size     = size;
    fvd->attr          = attr;

    vp->v_type       = (attr & FAT32_ATTR_DIRECTORY) ? VDIR : VREG;
    vp->v_op         = &fat32_vnode_ops;
    vp->v_data       = fvd;
    vp->v_mount_data = mnt;

    return vp;
}

/* ========== Mount / Unmount (Task 4) ========== */

/**
 * Parse sector 0 (BPB), validate FAT32, compute geometry.
 */
struct vnode *fat32_mount(struct block_device *dev, int flags) {
    (void)flags;

    if (!dev) {
        uart_puts("[FAT32] ERROR: NULL device\n");
        return NULL;
    }

    uart_puts("[FAT32] Mounting from device '");
    uart_puts(dev->name);
    uart_puts("'...\n");

    /* Allocate mount state */
    struct fat32_mount *mnt = (struct fat32_mount *)pfa_alloc_frame();
    if (!mnt) {
        uart_puts("[FAT32] ERROR: Failed to allocate mount state\n");
        return NULL;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->dev = dev;

    /* Read sector 0 (BPB) */
    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    if (fat32_read_sector(mnt, 0, sector_buf) != 0) {
        uart_puts("[FAT32] ERROR: Cannot read boot sector\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }

    struct fat32_bpb *bpb = (struct fat32_bpb *)sector_buf;

    /* Validate boot signature (bytes 510-511) */
    uint16_t boot_sig = *(uint16_t *)(sector_buf + 510);
    if (boot_sig != FAT32_BOOT_SIGNATURE) {
        uart_puts("[FAT32] ERROR: Invalid boot signature 0x");
        char hex[8];
        int_to_str(boot_sig, hex, 16);
        uart_puts(hex);
        uart_puts("\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }

    /* Validate FAT32-specific fields */
    if (bpb->BPB_BytsPerSec == 0 || bpb->BPB_SecPerClus == 0) {
        uart_puts("[FAT32] ERROR: Invalid BPB (zero BytsPerSec or SecPerClus)\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }
    if (bpb->BPB_RootEntCnt != 0) {
        uart_puts("[FAT32] ERROR: BPB_RootEntCnt != 0 (not FAT32?)\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }
    if (bpb->BPB_FATSz16 != 0) {
        uart_puts("[FAT32] WARNING: BPB_FATSz16 nonzero, using BPB_FATSz32\n");
    }
    if (bpb->BPB_FATSz32 == 0) {
        uart_puts("[FAT32] ERROR: BPB_FATSz32 is zero\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }

    /* Populate mount state from BPB */
    mnt->bytes_per_sec    = bpb->BPB_BytsPerSec;
    mnt->sec_per_clus     = bpb->BPB_SecPerClus;
    mnt->cluster_size     = (uint32_t)bpb->BPB_BytsPerSec * bpb->BPB_SecPerClus;
    mnt->num_fats         = bpb->BPB_NumFATs;
    mnt->fat_start_sector = bpb->BPB_RsvdSecCnt;
    mnt->fat_size_sectors = bpb->BPB_FATSz32;
    mnt->root_cluster     = bpb->BPB_RootClus;
    mnt->fsinfo_sector    = bpb->BPB_FSInfo;

    /* Compute data region start */
    uint32_t root_dir_sectors = 0;  /* Always 0 for FAT32 */
    mnt->data_start_sector = bpb->BPB_RsvdSecCnt
                           + (bpb->BPB_NumFATs * bpb->BPB_FATSz32)
                           + root_dir_sectors;

    /* Total sectors */
    uint32_t total_sectors = (bpb->BPB_TotSec32 != 0)
                           ? bpb->BPB_TotSec32
                           : bpb->BPB_TotSec16;

    /* Total data clusters */
    uint32_t data_sectors = total_sectors - mnt->data_start_sector;
    mnt->total_data_clusters = data_sectors / bpb->BPB_SecPerClus;

    /* Copy volume label */
    memcpy(mnt->volume_label, bpb->BS_VolLab, 11);
    mnt->volume_label[11] = '\0';
    /* Trim trailing spaces */
    for (int i = 10; i >= 0 && mnt->volume_label[i] == ' '; i--) {
        mnt->volume_label[i] = '\0';
    }

    /* Read FSInfo sector for free cluster hints */
    mnt->free_count = 0xFFFFFFFF;  /* Unknown */
    mnt->next_free  = FAT32_FIRST_DATA_CLUSTER;

    if (mnt->fsinfo_sector != 0 && mnt->fsinfo_sector != 0xFFFF) {
        struct fat32_fsinfo fsinfo;
        if (fat32_read_bytes(mnt, (uint64_t)mnt->fsinfo_sector * mnt->bytes_per_sec,
                             &fsinfo, sizeof(fsinfo)) == 0) {
            if (fsinfo.FSI_LeadSig == FAT32_FSINFO_LEAD_SIG &&
                fsinfo.FSI_StrucSig == FAT32_FSINFO_STRUC_SIG &&
                fsinfo.FSI_TrailSig == FAT32_FSINFO_TRAIL_SIG) {
                mnt->free_count = fsinfo.FSI_Free_Count;
                mnt->next_free  = fsinfo.FSI_Nxt_Free;
                uart_puts("[FAT32] FSInfo: free=");
                if (mnt->free_count == 0xFFFFFFFF) {
                    uart_puts("unknown");
                } else {
                    uart_putu(mnt->free_count);
                }
                uart_puts(", next_free=");
                uart_putu(mnt->next_free);
                uart_puts("\n");
            } else {
                uart_puts("[FAT32] FSInfo signatures invalid, ignoring\n");
            }
        }
    }

    /* Print mount summary */
    uart_puts("[FAT32] Volume: ");
    uart_puts(mnt->volume_label);
    uart_puts("\n");
    uart_puts("[FAT32] BytsPerSec=");
    uart_putu(mnt->bytes_per_sec);
    uart_puts(" SecPerClus=");
    uart_putu(mnt->sec_per_clus);
    uart_puts(" ClusterSize=");
    uart_putu(mnt->cluster_size);
    uart_puts("\n");
    uart_puts("[FAT32] FATs=");
    uart_putu(mnt->num_fats);
    uart_puts(" FATStart=");
    uart_putu(mnt->fat_start_sector);
    uart_puts(" FATSize=");
    uart_putu(mnt->fat_size_sectors);
    uart_puts("\n");
    uart_puts("[FAT32] DataStart=");
    uart_putu(mnt->data_start_sector);
    uart_puts(" DataClusters=");
    uart_putu(mnt->total_data_clusters);
    uart_puts(" RootCluster=");
    uart_putu(mnt->root_cluster);
    uart_puts("\n");

    /* Sanity check: FAT32 should have >= 65525 data clusters */
    if (mnt->total_data_clusters < 65525) {
        uart_puts("[FAT32] WARNING: Only ");
        uart_putu(mnt->total_data_clusters);
        uart_puts(" data clusters (< 65525, may be FAT16)\n");
    }

    /* Create root directory vnode */
    struct vnode *root_vp = fat32_create_vnode(mnt, mnt->root_cluster, 0,
                                               FAT32_ATTR_DIRECTORY);
    if (!root_vp) {
        uart_puts("[FAT32] ERROR: Failed to create root vnode\n");
        pfa_free_frame((uintptr_t)mnt);
        return NULL;
    }

    uart_puts("[FAT32] Mounted successfully\n");
    return root_vp;
}

int fat32_unmount(struct vnode *root) {
    if (!root) return -1;

    struct fat32_mount *mnt = (struct fat32_mount *)root->v_mount_data;
    if (!mnt) return -1;

    /* Sync dirty buffers */
    fat32_sync(root);

    /* Update FSInfo on disk */
    if (mnt->fsinfo_sector != 0 && mnt->fsinfo_sector != 0xFFFF) {
        struct fat32_fsinfo fsinfo;
        uint64_t fi_offset = (uint64_t)mnt->fsinfo_sector * mnt->bytes_per_sec;
        if (fat32_read_bytes(mnt, fi_offset, &fsinfo, sizeof(fsinfo)) == 0) {
            fsinfo.FSI_Free_Count = mnt->free_count;
            fsinfo.FSI_Nxt_Free   = mnt->next_free;
            fat32_write_bytes(mnt, fi_offset, &fsinfo, sizeof(fsinfo));
        }
    }

    /* Flush to device */
    buffer_sync_all(mnt->dev);

    /* Free root vnode data */
    if (root->v_data) {
        pfa_free_frame((uintptr_t)root->v_data);
    }
    pfa_free_frame((uintptr_t)mnt);
    pfa_free_frame((uintptr_t)root);

    uart_puts("[FAT32] Unmounted\n");
    return 0;
}

int fat32_sync(struct vnode *root) {
    if (!root || !root->v_mount_data) return -1;
    struct fat32_mount *mnt = (struct fat32_mount *)root->v_mount_data;
    return buffer_sync_all(mnt->dev);
}

/* ========== Filesystem Registration ========== */

static struct filesystem_ops fat32_fs_ops = {
    .mount   = fat32_mount,
    .unmount = fat32_unmount,
    .sync    = fat32_sync,
    .statfs  = NULL,
};

static struct filesystem fat32_fs = {
    .name = "fat32",
    .type = FS_TYPE_FAT32,
    .ops  = &fat32_fs_ops,
    .next = NULL,
};

void fat32_register(void) {
    fs_register(&fat32_fs);
    uart_puts("[FAT32] Filesystem type registered\n");
}

/* ========== Directory Reading (Task 6) ========== */

/**
 * Case-insensitive comparison for FAT32 short names.
 */
static int fat32_toupper(int c) {
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

static int fat32_name_cmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        int ca = fat32_toupper((unsigned char)*a);
        int cb = fat32_toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return fat32_toupper((unsigned char)*a) - fat32_toupper((unsigned char)*b);
}

/**
 * Convert an 8.3 short name from disk format to a human-readable string.
 * Honors the DIR_NTRes case flags (Microsoft extension):
 *   bit 3 (0x08) = name part is lowercase
 *   bit 4 (0x10) = extension part is lowercase
 * E.g., "HELLO   TXT" with nt_res=0x18 -> "hello.txt"
 */
static void fat32_shortname_to_str(const uint8_t *raw, char *out,
                                    uint8_t nt_res) {
    int pos = 0;
    int name_lower = (nt_res & 0x08);  /* bit 3: name lowercase */
    int ext_lower  = (nt_res & 0x10);  /* bit 4: ext lowercase  */

    /* Copy name part (first 8 bytes), trimming trailing spaces */
    int name_end = 7;
    while (name_end >= 0 && raw[name_end] == ' ') name_end--;
    for (int i = 0; i <= name_end; i++) {
        char c = (char)raw[i];
        if (name_lower && c >= 'A' && c <= 'Z') c += 32;
        out[pos++] = c;
    }

    /* Copy extension (bytes 8-10), if present */
    int ext_end = 10;
    while (ext_end >= 8 && raw[ext_end] == ' ') ext_end--;
    if (ext_end >= 8) {
        out[pos++] = '.';
        for (int i = 8; i <= ext_end; i++) {
            char c = (char)raw[i];
            if (ext_lower && c >= 'A' && c <= 'Z') c += 32;
            out[pos++] = c;
        }
    }

    out[pos] = '\0';
}

/**
 * Extract characters from an LFN entry into a UCS-2 -> ASCII buffer.
 * seq_num is 1-based; characters go at offset (seq_num-1)*13.
 */
static void fat32_lfn_extract(const struct fat32_lfn_entry *lfn,
                               char *name_buf, int seq_num) {
    int base = (seq_num - 1) * FAT32_LFN_CHARS_PER;

    /* Name1: 5 chars */
    for (int i = 0; i < 5; i++) {
        uint16_t c = lfn->LDIR_Name1[i];
        if (c == 0x0000 || c == 0xFFFF) {
            name_buf[base + i] = '\0';
            return;
        }
        name_buf[base + i] = (c < 128) ? (char)c : '?';
    }
    /* Name2: 6 chars */
    for (int i = 0; i < 6; i++) {
        uint16_t c = lfn->LDIR_Name2[i];
        if (c == 0x0000 || c == 0xFFFF) {
            name_buf[base + 5 + i] = '\0';
            return;
        }
        name_buf[base + 5 + i] = (c < 128) ? (char)c : '?';
    }
    /* Name3: 2 chars */
    for (int i = 0; i < 2; i++) {
        uint16_t c = lfn->LDIR_Name3[i];
        if (c == 0x0000 || c == 0xFFFF) {
            name_buf[base + 11 + i] = '\0';
            return;
        }
        name_buf[base + 11 + i] = (c < 128) ? (char)c : '?';
    }
}

/**
 * Read directory entries from a directory starting at 'start_cluster'.
 * Calls 'callback' for each valid entry found.
 * Returns 0 on success, negative on error.
 *
 * The callback receives:
 *   - name: resolved filename (LFN if available, else short name)
 *   - dirent: pointer to the short (8.3) directory entry
 *   - ctx: opaque context pointer
 * Return 0 from callback to continue, nonzero to stop.
 */
typedef int (*fat32_dir_callback)(const char *name,
                                   const struct fat32_dirent *dirent,
                                   uint32_t dir_byte_offset,
                                   void *ctx);

static int fat32_iterate_dir(struct fat32_mount *mnt, uint32_t start_cluster,
                              fat32_dir_callback callback, void *ctx) {
    uint8_t *cluster_buf = (uint8_t *)pfa_alloc_frame();
    if (!cluster_buf) {
        uart_puts("[FAT32] iterate_dir: pfa_alloc_frame failed!\n");
        return -1;
    }

    uart_puts("[FAT32] iterate_dir: start_cluster=");
    uart_putu(start_cluster);
    uart_puts(" cluster_size=");
    uart_putu(mnt->cluster_size);
    uart_puts(" buf=0x");
    uart_puthex((uintptr_t)cluster_buf);
    uart_puts("\n");

    /* We may need more than one page for large clusters */
    /* For now, support clusters up to 4096 bytes (1 page).
     * For larger clusters, we'd need multiple pages. */
    if (mnt->cluster_size > 4096) {
        uart_puts("[FAT32] WARNING: Cluster > 4096 not fully supported in iterate\n");
    }

    char lfn_buf[FAT32_MAX_FILENAME + 1];
    int lfn_active = 0;
    uint8_t lfn_checksum = 0;

    uint32_t cluster = start_cluster;
    uint32_t cluster_idx = 0;
    int total_entries_seen = 0;
    while (cluster >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(cluster)) {
        uart_puts("[FAT32] iterate_dir: reading cluster ");
        uart_putu(cluster);
        uart_puts(" (idx=");
        uart_putu(cluster_idx);
        uart_puts(")\n");

        /* Read this cluster */
        if (fat32_read_cluster(mnt, cluster, cluster_buf) != 0) {
            uart_puts("[FAT32] iterate_dir: fat32_read_cluster FAILED for cluster ");
            uart_putu(cluster);
            uart_puts("\n");
            pfa_free_frame((uintptr_t)cluster_buf);
            return -1;
        }

        /* Dump first 32 bytes of cluster for debugging */
        uart_puts("[FAT32] iterate_dir: first 32 bytes: ");
        for (int dbg = 0; dbg < 32; dbg++) {
            uart_puthex(cluster_buf[dbg]);
            uart_puts(" ");
        }
        uart_puts("\n");

        uint32_t entries_per_cluster = mnt->cluster_size / FAT32_DIRENT_SIZE;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dirent *de = (struct fat32_dirent *)
                                      (cluster_buf + i * FAT32_DIRENT_SIZE);

            /* End of directory? */
            if (FAT32_IS_DIRENT_END(de)) {
                uart_puts("[FAT32] iterate_dir: hit end-of-dir at entry ");
                uart_putu(i);
                uart_puts(" (total seen=");
                uart_putu(total_entries_seen);
                uart_puts("), first byte=0x");
                uart_puthex(de->DIR_Name[0]);
                uart_puts("\n");
                pfa_free_frame((uintptr_t)cluster_buf);
                return 0;
            }

            /* Deleted entry */
            if (FAT32_IS_DIRENT_FREE(de)) {
                lfn_active = 0;
                continue;
            }

            total_entries_seen++;

            /* LFN entry */
            if (FAT32_IS_LFN(de)) {
                struct fat32_lfn_entry *lfn = (struct fat32_lfn_entry *)de;
                int seq = lfn->LDIR_Ord & FAT32_LFN_SEQ_MASK;

                if (lfn->LDIR_Ord & FAT32_LFN_LAST_ENTRY) {
                    /* Start of a new LFN sequence */
                    memset(lfn_buf, 0, sizeof(lfn_buf));
                    lfn_active = 1;
                    lfn_checksum = lfn->LDIR_Chksum;
                }

                if (lfn_active && seq >= 1 && seq <= 20) {
                    fat32_lfn_extract(lfn, lfn_buf, seq);
                }
                continue;
            }

            /* Regular short entry — skip volume labels */
            if (de->DIR_Attr & FAT32_ATTR_VOLUME_ID) {
                lfn_active = 0;
                continue;
            }

            /* Determine the name to use */
            char name[FAT32_MAX_FILENAME + 1];
            if (lfn_active) {
                /* Validate LFN checksum against this short name */
                uint8_t computed = fat32_lfn_checksum(de->DIR_Name);
                if (computed == lfn_checksum) {
                    /* Use LFN */
                    size_t len = strlen(lfn_buf);
                    if (len > FAT32_MAX_FILENAME) len = FAT32_MAX_FILENAME;
                    memcpy(name, lfn_buf, len);
                    name[len] = '\0';
                } else {
                    /* Checksum mismatch, fall back to short name */
                    fat32_shortname_to_str(de->DIR_Name, name, de->DIR_NTRes);
                }
            } else {
                fat32_shortname_to_str(de->DIR_Name, name, de->DIR_NTRes);
            }

            lfn_active = 0;

            /* Call callback with directory byte offset */
            uint32_t entry_offset = cluster_idx * mnt->cluster_size
                                  + i * FAT32_DIRENT_SIZE;
            if (callback(name, de, entry_offset, ctx) != 0) {
                pfa_free_frame((uintptr_t)cluster_buf);
                return 0;  /* Callback requested stop */
            }
        }

        /* Next cluster in the chain */
        cluster_idx++;
        uint32_t next_cluster = fat32_fat_read(mnt, cluster);
        uart_puts("[FAT32] iterate_dir: next cluster in chain: ");
        uart_putu(next_cluster);
        uart_puts(" (0x");
        uart_puthex(next_cluster);
        uart_puts(")\n");
        cluster = next_cluster;
    }

    uart_puts("[FAT32] iterate_dir: loop exited, cluster=0x");
    uart_puthex(cluster);
    uart_puts(" total_entries_seen=");
    uart_putu(total_entries_seen);
    uart_puts("\n");

    pfa_free_frame((uintptr_t)cluster_buf);
    return 0;
}

/* ========== Vnode Operations ========== */

static int fat32_vop_open(struct vnode *vp) {
    (void)vp;
    return 0;
}

static int fat32_vop_close(struct vnode *vp) {
    /* Free the vnode data when done
     * Note: In a real FS we'd use ref counting. For now, simple free. */
    (void)vp;
    return 0;
}

static int64_t fat32_vop_getsize(struct vnode *vp) {
    if (!vp || !vp->v_data) return -1;
    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
    return (int64_t)fvd->file_size;
}

/* --- vop_read_at: read file data at offset (Task 7) --- */

static int fat32_vop_read_at(struct vnode *vp, void *buf, size_t nbyte,
                              uint64_t offset) {
    if (!vp || !buf || !vp->v_data || !vp->v_mount_data) return -1;

    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)vp->v_mount_data;

    /* Cannot read past end of file */
    if (offset >= fvd->file_size) return 0;
    if (offset + nbyte > fvd->file_size) {
        nbyte = fvd->file_size - offset;
    }
    if (nbyte == 0) return 0;

    uint32_t clus_size = mnt->cluster_size;
    /* Calculate which cluster in the chain to start from */
    uint32_t skip_clusters = (uint32_t)(offset / clus_size);
    uint32_t offset_in_cluster = (uint32_t)(offset % clus_size);

    /* Walk the cluster chain to the starting cluster */
    uint32_t cur = fvd->start_cluster;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cur = fat32_fat_read(mnt, cur);
        if (cur == 0 || FAT32_IS_EOC(cur)) return 0;
    }

    /* Allocate a temp cluster buffer */
    uint8_t *clus_buf = (uint8_t *)pfa_alloc_frame();
    if (!clus_buf) return -1;

    size_t bytes_read = 0;
    uint8_t *dst = (uint8_t *)buf;

    while (bytes_read < nbyte && cur >= FAT32_FIRST_DATA_CLUSTER
           && !FAT32_IS_EOC(cur)) {
        /* Read current cluster */
        if (fat32_read_cluster(mnt, cur, clus_buf) != 0) {
            pfa_free_frame((uintptr_t)clus_buf);
            return (bytes_read > 0) ? (int)bytes_read : -1;
        }

        /* How much to copy from this cluster */
        size_t avail = clus_size - offset_in_cluster;
        size_t to_copy = (nbyte - bytes_read < avail) ? (nbyte - bytes_read) : avail;

        memcpy(dst + bytes_read, clus_buf + offset_in_cluster, to_copy);
        bytes_read += to_copy;
        offset_in_cluster = 0;  /* Subsequent clusters start from byte 0 */

        /* Next cluster */
        cur = fat32_fat_read(mnt, cur);
    }

    pfa_free_frame((uintptr_t)clus_buf);
    return (int)bytes_read;
}

/* --- Lookup callback context --- */

struct lookup_ctx {
    const char *target_name;
    struct fat32_mount *mnt;
    struct vnode **result;
    uint32_t dir_start_cluster;
    int found;
};

static int lookup_callback(const char *name, const struct fat32_dirent *de,
                            uint32_t dir_byte_offset, void *ctx_ptr) {
    struct lookup_ctx *ctx = (struct lookup_ctx *)ctx_ptr;

    if (fat32_name_cmp_ci(name, ctx->target_name) == 0) {
        uint32_t cluster = FAT32_DIRENT_CLUSTER(de);
        uint32_t size    = de->DIR_FileSize;
        uint8_t  attr    = de->DIR_Attr;

        struct vnode *vp = fat32_create_vnode(ctx->mnt, cluster, size, attr);
        if (vp) {
            /* Record parent dir info so writes can update the dirent */
            struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
            fvd->dir_cluster      = ctx->dir_start_cluster;
            fvd->dir_entry_offset = dir_byte_offset;
        }
        *ctx->result = vp;
        ctx->found = 1;
        return 1;  /* Stop iteration */
    }
    return 0;  /* Continue */
}

static int fat32_vop_lookup(struct vnode *dir, const char *name,
                             struct vnode **result) {
    if (!dir || !name || !result) return -1;
    if (dir->v_type != VDIR) return -1;

    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)dir->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)dir->v_mount_data;
    if (!fvd || !mnt) return -1;

    struct lookup_ctx ctx = {
        .target_name = name,
        .mnt = mnt,
        .result = result,
        .dir_start_cluster = fvd->start_cluster,
        .found = 0,
    };

    int iter_rc = fat32_iterate_dir(mnt, fvd->start_cluster, lookup_callback, &ctx);

    if (iter_rc < 0) {
        uart_puts("[FAT32] lookup: iterate_dir failed for '");
        uart_puts(name);
        uart_puts("'\n");
        return -1;
    }

    if (!ctx.found) {
        uart_puts("[FAT32] lookup: '");
        uart_puts(name);
        uart_puts("' not found in dir cluster ");
        uart_putu(fvd->start_cluster);
        uart_puts("\n");
    }

    return ctx.found ? 0 : -2;  /* -2 = not found (ENOENT) */
}

/* --- Readdir callback context --- */

struct readdir_ctx {
    struct vfs_dirent *dent;
    uint64_t *offset;       /* Current entry index */
    uint64_t target;        /* The entry index we want */
    uint64_t current;       /* Counter as we iterate */
    int found;
};

static int readdir_callback(const char *name, const struct fat32_dirent *de,
                             uint32_t dir_byte_offset, void *ctx_ptr) {
    (void)dir_byte_offset;
    struct readdir_ctx *ctx = (struct readdir_ctx *)ctx_ptr;

    if (ctx->current == ctx->target) {
        /* This is the entry we want */
        size_t len = strlen(name);
        if (len > VFS_NAME_MAX) len = VFS_NAME_MAX;
        memcpy(ctx->dent->d_name, name, len);
        ctx->dent->d_name[len] = '\0';

        ctx->dent->d_type = (de->DIR_Attr & FAT32_ATTR_DIRECTORY) ? VDIR : VREG;
        ctx->dent->d_size = de->DIR_FileSize;
        ctx->dent->d_cluster = FAT32_DIRENT_CLUSTER(de);

        *ctx->offset = ctx->current + 1;  /* Advance for next call */
        ctx->found = 1;
        return 1;  /* Stop */
    }

    ctx->current++;
    return 0;  /* Continue */
}

static int fat32_vop_readdir(struct vnode *dir, struct vfs_dirent *dent,
                              uint64_t *offset) {
    if (!dir || !dent || !offset) return -1;
    if (dir->v_type != VDIR) return -1;

    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)dir->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)dir->v_mount_data;
    if (!fvd || !mnt) return -1;

    struct readdir_ctx ctx = {
        .dent = dent,
        .offset = offset,
        .target = *offset,
        .current = 0,
        .found = 0,
    };

    int iter_rc = fat32_iterate_dir(mnt, fvd->start_cluster, readdir_callback, &ctx);

    if (iter_rc < 0) {
        uart_puts("[FAT32] readdir: iterate_dir failed\n");
        return -1;
    }

    if (ctx.found) return 0;
    return 1;  /* No more entries */
}

/* ========== Directory Entry Update Helper (Task 8) ========== */

/**
 * Update a file's directory entry on disk (file size and start cluster).
 * Uses dir_cluster (start cluster of parent directory) and
 * dir_entry_offset (byte offset from start of directory data) stored in fvd.
 */
static int fat32_update_dirent(struct fat32_mount *mnt,
                                struct fat32_vnode_data *fvd) {
    if (fvd->dir_cluster < FAT32_FIRST_DATA_CLUSTER) return -1;

    uint32_t clus_size      = mnt->cluster_size;
    uint32_t chain_pos      = fvd->dir_entry_offset / clus_size;
    uint32_t off_in_cluster = fvd->dir_entry_offset % clus_size;

    /* Walk directory cluster chain to find the right cluster */
    uint32_t dir_clus = fvd->dir_cluster;
    for (uint32_t i = 0; i < chain_pos; i++) {
        dir_clus = fat32_fat_read(mnt, dir_clus);
        if (dir_clus == 0 || FAT32_IS_EOC(dir_clus)) return -1;
    }

    /* Read the directory cluster */
    uint8_t *buf = (uint8_t *)pfa_alloc_frame();
    if (!buf) return -1;

    if (fat32_read_cluster(mnt, dir_clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    /* Patch the 32-byte entry */
    struct fat32_dirent *de = (struct fat32_dirent *)(buf + off_in_cluster);
    de->DIR_FileSize  = fvd->file_size;
    de->DIR_FstClusHI = (uint16_t)(fvd->start_cluster >> 16);
    de->DIR_FstClusLO = (uint16_t)(fvd->start_cluster & 0xFFFF);

    int ret = fat32_write_cluster(mnt, dir_clus, buf);
    pfa_free_frame((uintptr_t)buf);
    return ret;
}

/* ========== 8.3 Short Name Generation ========== */

/**
 * Convert a human-readable filename to FAT32 8.3 short name format.
 * Rules: uppercase, space-padded, no dot separator.
 * E.g. "hello.txt" -> "HELLO   TXT"
 */
static void fat32_str_to_shortname(const char *name, uint8_t *out) {
    memset(out, ' ', 11);

    /* Find the last dot for extension splitting */
    const char *last_dot = NULL;
    for (int j = 0; name[j]; j++) {
        if (name[j] == '.') last_dot = &name[j];
    }

    /* Fill name field (bytes 0-7) */
    int i = 0, o = 0;
    while (name[i] && o < 8) {
        if (&name[i] == last_dot) {
            i++;  /* Skip the dot, move to extension */
            break;
        }
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;  /* toupper */
        if (c == ' ' || c == '.') continue;  /* skip invalid */
        out[o++] = (uint8_t)c;
    }

    /* Fill extension field (bytes 8-10) */
    if (last_dot) {
        const char *ext = last_dot + 1;
        int e = 0;
        while (*ext && e < 3) {
            char c = *ext++;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[8 + e++] = (uint8_t)c;
        }
    }
}

/**
 * Compute the DIR_NTRes case flags for an 8.3-compatible filename.
 *   bit 3 (0x08): name part was all lowercase
 *   bit 4 (0x10): extension part was all lowercase
 * Returns 0 if the name has mixed case (would need LFN).
 */
static uint8_t fat32_compute_nt_res(const char *name) {
    uint8_t flags = 0;

    /* Split at last dot */
    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') dot = p;
    }

    /* Check name part case */
    int has_upper = 0, has_lower = 0;
    for (const char *p = name; *p && p != dot; p++) {
        if (*p >= 'A' && *p <= 'Z') has_upper = 1;
        if (*p >= 'a' && *p <= 'z') has_lower = 1;
    }
    /* Pure lowercase name → set bit 3 */
    if (has_lower && !has_upper) flags |= 0x08;

    /* Check extension part case */
    if (dot) {
        has_upper = 0;
        has_lower = 0;
        for (const char *p = dot + 1; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') has_upper = 1;
            if (*p >= 'a' && *p <= 'z') has_lower = 1;
        }
        /* Pure lowercase extension → set bit 4 */
        if (has_lower && !has_upper) flags |= 0x10;
    }

    return flags;
}

/* ========== Free Directory Entry Finder ========== */

/**
 * Scan a directory for a free 32-byte entry slot.
 * If none found, extends the directory by one cluster.
 * On success, *out_offset receives the byte offset from the
 * start of the directory's cluster chain.
 */
static int fat32_find_free_dirent(struct fat32_mount *mnt, uint32_t dir_start,
                                   uint32_t *out_offset) {
    uint8_t *cluster_buf = (uint8_t *)pfa_alloc_frame();
    if (!cluster_buf) return -1;

    uint32_t clus_size       = mnt->cluster_size;
    uint32_t entries_per_clus = clus_size / FAT32_DIRENT_SIZE;
    uint32_t cluster         = dir_start;
    uint32_t cluster_idx     = 0;

    while (cluster >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(cluster)) {
        if (fat32_read_cluster(mnt, cluster, cluster_buf) != 0) {
            pfa_free_frame((uintptr_t)cluster_buf);
            return -1;
        }

        for (uint32_t i = 0; i < entries_per_clus; i++) {
            uint8_t first_byte = cluster_buf[i * FAT32_DIRENT_SIZE];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                /* Free slot found */
                *out_offset = cluster_idx * clus_size + i * FAT32_DIRENT_SIZE;
                pfa_free_frame((uintptr_t)cluster_buf);
                return 0;
            }
        }

        cluster = fat32_fat_read(mnt, cluster);
        cluster_idx++;
    }

    pfa_free_frame((uintptr_t)cluster_buf);

    /* No free slot — extend the directory by one cluster */
    uint32_t last = dir_start;
    uint32_t next = fat32_fat_read(mnt, last);
    while (next >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(next)) {
        last = next;
        next = fat32_fat_read(mnt, last);
    }

    uint32_t new_clus = fat32_extend_chain(mnt, last);
    if (new_clus == 0) return -1;

    /* Zero the new cluster (all entries become end-of-directory) */
    uint8_t *zero_buf = (uint8_t *)pfa_alloc_frame();
    if (zero_buf) {
        memset(zero_buf, 0, clus_size);
        fat32_write_cluster(mnt, new_clus, zero_buf);
        pfa_free_frame((uintptr_t)zero_buf);
    }

    *out_offset = cluster_idx * clus_size;  /* First entry in new cluster */
    return 0;
}

/* ========== vop_write_at: write file data at offset (Task 8) ========== */

static int fat32_vop_write_at(struct vnode *vp, const void *buf, size_t nbyte,
                               uint64_t offset) {
    if (!vp || !buf || !vp->v_data || !vp->v_mount_data) return -1;
    if (vp->v_type != VREG) return -1;

    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)vp->v_mount_data;

    if (nbyte == 0) return 0;

    uint32_t clus_size = mnt->cluster_size;

    /* If file has no clusters yet (new / empty file), allocate the first */
    if (fvd->start_cluster < FAT32_FIRST_DATA_CLUSTER) {
        uint32_t new_clus = fat32_alloc_cluster(mnt);
        if (new_clus == 0) {
            uart_puts("[FAT32] write: cannot allocate first cluster\n");
            return -1;
        }
        fvd->start_cluster = new_clus;

        /* Zero the fresh cluster */
        uint8_t *z = (uint8_t *)pfa_alloc_frame();
        if (z) {
            memset(z, 0, clus_size);
            fat32_write_cluster(mnt, new_clus, z);
            pfa_free_frame((uintptr_t)z);
        }
    }

    /* How many clusters does the file need after this write? */
    uint64_t end_offset     = offset + nbyte;
    uint32_t clusters_needed = (uint32_t)((end_offset + clus_size - 1) / clus_size);

    /* Extend the cluster chain if necessary */
    uint32_t cur_chain_len = fat32_chain_length(mnt, fvd->start_cluster);
    if (clusters_needed > cur_chain_len) {
        /* Walk to the last cluster in the existing chain */
        uint32_t last = fvd->start_cluster;
        uint32_t nxt  = fat32_fat_read(mnt, last);
        while (nxt >= FAT32_FIRST_DATA_CLUSTER && !FAT32_IS_EOC(nxt)) {
            last = nxt;
            nxt = fat32_fat_read(mnt, last);
        }

        for (uint32_t i = cur_chain_len; i < clusters_needed; i++) {
            uint32_t added = fat32_extend_chain(mnt, last);
            if (added == 0) {
                /* Disk full — truncate the write to available space */
                uint64_t max_end = (uint64_t)i * clus_size;
                if (max_end <= offset) {
                    uart_puts("[FAT32] write: no space\n");
                    return -1;
                }
                nbyte      = (size_t)(max_end - offset);
                end_offset = offset + nbyte;
                break;
            }
            /* Zero the newly allocated cluster */
            uint8_t *z = (uint8_t *)pfa_alloc_frame();
            if (z) {
                memset(z, 0, clus_size);
                fat32_write_cluster(mnt, added, z);
                pfa_free_frame((uintptr_t)z);
            }
            last = added;
        }
    }

    /* Walk the chain to the first cluster we need to write */
    uint32_t skip_clusters  = (uint32_t)(offset / clus_size);
    uint32_t off_in_cluster = (uint32_t)(offset % clus_size);

    uint32_t cur = fvd->start_cluster;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cur = fat32_fat_read(mnt, cur);
        if (cur == 0 || FAT32_IS_EOC(cur)) return -1;
    }

    /* Allocate temp cluster buffer for read-modify-write */
    uint8_t *clus_buf = (uint8_t *)pfa_alloc_frame();
    if (!clus_buf) return -1;

    size_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (bytes_written < nbyte && cur >= FAT32_FIRST_DATA_CLUSTER
           && !FAT32_IS_EOC(cur)) {
        /* Read current cluster */
        if (fat32_read_cluster(mnt, cur, clus_buf) != 0) {
            pfa_free_frame((uintptr_t)clus_buf);
            return (bytes_written > 0) ? (int)bytes_written : -1;
        }

        /* Copy caller data into the cluster buffer */
        size_t avail   = clus_size - off_in_cluster;
        size_t to_copy = (nbyte - bytes_written < avail)
                       ? (nbyte - bytes_written) : avail;

        memcpy(clus_buf + off_in_cluster, src + bytes_written, to_copy);

        /* Write the full cluster back to disk */
        if (fat32_write_cluster(mnt, cur, clus_buf) != 0) {
            pfa_free_frame((uintptr_t)clus_buf);
            return (bytes_written > 0) ? (int)bytes_written : -1;
        }

        bytes_written  += to_copy;
        off_in_cluster  = 0;  /* Subsequent clusters start at byte 0 */
        cur = fat32_fat_read(mnt, cur);
    }

    pfa_free_frame((uintptr_t)clus_buf);

    /* Update in-memory file size if we wrote past the current end */
    if (end_offset > fvd->file_size) {
        fvd->file_size = (uint32_t)end_offset;
    }

    /* Persist size + cluster to the on-disk directory entry */
    fat32_update_dirent(mnt, fvd);

    /* Flush all dirty buffers to disk so changes survive reboot */
    buffer_sync_all(mnt->dev);

    return (int)bytes_written;
}

/* ========== vop_create: create a new file in a directory (Task 8) ========== */

static int fat32_vop_create(struct vnode *dir, const char *name,
                             struct vnode **result) {
    if (!dir || !name || !result) return -1;
    if (dir->v_type != VDIR) return -1;

    struct fat32_vnode_data *dir_fvd = (struct fat32_vnode_data *)dir->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)dir->v_mount_data;
    if (!dir_fvd || !mnt) return -1;

    /* Check if a file with this name already exists */
    struct vnode *existing = NULL;
    if (fat32_vop_lookup(dir, name, &existing) == 0) {
        *result = existing;
        return -3;  /* EEXIST */
    }

    /* Generate 8.3 short name */
    uint8_t short_name[11];
    fat32_str_to_shortname(name, short_name);

    /* Find a free 32-byte slot in the parent directory */
    uint32_t entry_offset;
    if (fat32_find_free_dirent(mnt, dir_fvd->start_cluster,
                                &entry_offset) != 0) {
        uart_puts("[FAT32] create: no free directory entry\n");
        return -1;
    }

    /* Compute NTRes case flags from the original name */
    uint8_t nt_res = fat32_compute_nt_res(name);

    /* Build a fresh directory entry (zero-length file, no cluster yet) */
    struct fat32_dirent new_de;
    memset(&new_de, 0, sizeof(new_de));
    memcpy(new_de.DIR_Name, short_name, 11);
    new_de.DIR_Attr      = FAT32_ATTR_ARCHIVE;
    new_de.DIR_NTRes     = nt_res;
    new_de.DIR_FileSize  = 0;
    new_de.DIR_FstClusHI = 0;
    new_de.DIR_FstClusLO = 0;
    /* Timestamps left at zero — a real driver would set a date here */

    /* Write the entry into the parent directory cluster */
    uint32_t clus_size      = mnt->cluster_size;
    uint32_t chain_pos      = entry_offset / clus_size;
    uint32_t off_in_cluster = entry_offset % clus_size;

    uint32_t clus = dir_fvd->start_cluster;
    for (uint32_t i = 0; i < chain_pos; i++) {
        clus = fat32_fat_read(mnt, clus);
        if (clus == 0 || FAT32_IS_EOC(clus)) return -1;
    }

    uint8_t *buf = (uint8_t *)pfa_alloc_frame();
    if (!buf) return -1;

    if (fat32_read_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    memcpy(buf + off_in_cluster, &new_de, sizeof(new_de));

    if (fat32_write_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    pfa_free_frame((uintptr_t)buf);

    /* Create a vnode for the new file */
    struct vnode *vp = fat32_create_vnode(mnt, 0, 0, FAT32_ATTR_ARCHIVE);
    if (!vp) return -1;

    /* Record parent directory info so future writes can update the dirent */
    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
    fvd->dir_cluster      = dir_fvd->start_cluster;
    fvd->dir_entry_offset = entry_offset;

    uart_puts("[FAT32] Created file '");
    uart_puts(name);
    uart_puts("'\n");

    /* Flush all dirty buffers to disk so changes survive reboot */
    buffer_sync_all(mnt->dev);

    *result = vp;
    return 0;
}

/* ========== vop_mkdir: create a subdirectory (Task 9) ========== */

/**
 * Write a 32-byte directory entry into a directory cluster at a specific offset.
 * Helper shared by mkdir and potentially other entry-creation paths.
 */
static int fat32_write_dirent_at(struct fat32_mount *mnt,
                                  uint32_t dir_start_cluster,
                                  uint32_t entry_offset,
                                  const struct fat32_dirent *de) {
    uint32_t clus_size      = mnt->cluster_size;
    uint32_t chain_pos      = entry_offset / clus_size;
    uint32_t off_in_cluster = entry_offset % clus_size;

    uint32_t clus = dir_start_cluster;
    for (uint32_t i = 0; i < chain_pos; i++) {
        clus = fat32_fat_read(mnt, clus);
        if (clus == 0 || FAT32_IS_EOC(clus)) return -1;
    }

    uint8_t *buf = (uint8_t *)pfa_alloc_frame();
    if (!buf) return -1;

    if (fat32_read_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    memcpy(buf + off_in_cluster, de, sizeof(*de));

    if (fat32_write_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    pfa_free_frame((uintptr_t)buf);
    return 0;
}

static int fat32_vop_mkdir(struct vnode *dir, const char *name,
                            struct vnode **result) {
    if (!dir || !name || !result) return -1;
    if (dir->v_type != VDIR) return -1;

    struct fat32_vnode_data *dir_fvd = (struct fat32_vnode_data *)dir->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)dir->v_mount_data;
    if (!dir_fvd || !mnt) return -1;

    /* Check for duplicate name */
    struct vnode *existing = NULL;
    if (fat32_vop_lookup(dir, name, &existing) == 0) {
        *result = existing;
        return -3;  /* EEXIST */
    }

    /* Allocate one cluster for the new directory's data */
    uint32_t new_clus = fat32_alloc_cluster(mnt);
    if (new_clus == 0) {
        uart_puts("[FAT32] mkdir: cannot allocate cluster\n");
        return -1;
    }

    /* Zero the newly allocated cluster */
    uint8_t *clus_buf = (uint8_t *)pfa_alloc_frame();
    if (!clus_buf) {
        fat32_free_chain(mnt, new_clus);
        return -1;
    }
    memset(clus_buf, 0, mnt->cluster_size);

    /* Write "." entry — points to itself */
    struct fat32_dirent dot;
    memset(&dot, 0, sizeof(dot));
    memcpy(dot.DIR_Name, ".          ", 11);
    dot.DIR_Attr      = FAT32_ATTR_DIRECTORY;
    dot.DIR_FstClusHI = (uint16_t)(new_clus >> 16);
    dot.DIR_FstClusLO = (uint16_t)(new_clus & 0xFFFF);
    memcpy(clus_buf + 0 * FAT32_DIRENT_SIZE, &dot, sizeof(dot));

    /* Write ".." entry — points to parent directory */
    struct fat32_dirent dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    memcpy(dotdot.DIR_Name, "..         ", 11);
    dotdot.DIR_Attr      = FAT32_ATTR_DIRECTORY;
    uint32_t parent_clus = dir_fvd->start_cluster;
    /* If parent is the root directory, FAT32 spec says cluster should be 0 */
    if (parent_clus == mnt->root_cluster) parent_clus = 0;
    dotdot.DIR_FstClusHI = (uint16_t)(parent_clus >> 16);
    dotdot.DIR_FstClusLO = (uint16_t)(parent_clus & 0xFFFF);
    memcpy(clus_buf + 1 * FAT32_DIRENT_SIZE, &dotdot, sizeof(dotdot));

    /* Write the cluster data (rest is all zeros = end-of-dir markers) */
    if (fat32_write_cluster(mnt, new_clus, clus_buf) != 0) {
        pfa_free_frame((uintptr_t)clus_buf);
        fat32_free_chain(mnt, new_clus);
        return -1;
    }
    pfa_free_frame((uintptr_t)clus_buf);

    /* Find a free slot in the parent directory for this new entry */
    uint32_t entry_offset;
    if (fat32_find_free_dirent(mnt, dir_fvd->start_cluster,
                                &entry_offset) != 0) {
        uart_puts("[FAT32] mkdir: no free directory entry\n");
        fat32_free_chain(mnt, new_clus);
        return -1;
    }

    /* Build the directory entry for the parent listing */
    uint8_t short_name[11];
    fat32_str_to_shortname(name, short_name);
    uint8_t nt_res = fat32_compute_nt_res(name);

    struct fat32_dirent new_de;
    memset(&new_de, 0, sizeof(new_de));
    memcpy(new_de.DIR_Name, short_name, 11);
    new_de.DIR_Attr      = FAT32_ATTR_DIRECTORY;
    new_de.DIR_NTRes     = nt_res;
    new_de.DIR_FileSize  = 0;  /* Directories always have size 0 */
    new_de.DIR_FstClusHI = (uint16_t)(new_clus >> 16);
    new_de.DIR_FstClusLO = (uint16_t)(new_clus & 0xFFFF);

    /* Write the entry into the parent directory */
    if (fat32_write_dirent_at(mnt, dir_fvd->start_cluster,
                               entry_offset, &new_de) != 0) {
        fat32_free_chain(mnt, new_clus);
        return -1;
    }

    /* Create vnode */
    struct vnode *vp = fat32_create_vnode(mnt, new_clus, 0, FAT32_ATTR_DIRECTORY);
    if (!vp) return -1;

    struct fat32_vnode_data *fvd = (struct fat32_vnode_data *)vp->v_data;
    fvd->dir_cluster      = dir_fvd->start_cluster;
    fvd->dir_entry_offset = entry_offset;

    uart_puts("[FAT32] Created directory '");
    uart_puts(name);
    uart_puts("'\n");

    /* Flush all dirty buffers to disk so changes survive reboot */
    buffer_sync_all(mnt->dev);

    *result = vp;
    return 0;
}

/* ========== vop_remove: delete a file or empty directory (Task 9) ========== */

/**
 * Context for checking whether a directory is empty.
 * It is empty if it contains only "." and ".." (or nothing).
 */
struct empty_check_ctx {
    int count;  /* Number of real entries (not . or ..) */
};

static int empty_check_callback(const char *name,
                                 const struct fat32_dirent *de,
                                 uint32_t dir_byte_offset,
                                 void *ctx_ptr) {
    (void)de;
    (void)dir_byte_offset;
    struct empty_check_ctx *ctx = (struct empty_check_ctx *)ctx_ptr;

    /* Skip "." and ".." */
    if (name[0] == '.' && (name[1] == '\0' ||
        (name[1] == '.' && name[2] == '\0'))) {
        return 0;  /* continue */
    }

    ctx->count++;
    return 1;  /* found a real entry — stop, directory is not empty */
}

static int fat32_dir_is_empty(struct fat32_mount *mnt, uint32_t start_cluster) {
    struct empty_check_ctx ctx = { .count = 0 };
    fat32_iterate_dir(mnt, start_cluster, empty_check_callback, &ctx);
    return (ctx.count == 0) ? 1 : 0;
}

/**
 * Context for finding a specific entry so we can mark it deleted.
 */
struct remove_ctx {
    const char *target_name;
    struct fat32_mount *mnt;
    uint32_t dir_start_cluster;
    uint32_t found_cluster;       /* Start cluster of the found entry */
    uint8_t  found_attr;
    uint32_t found_entry_offset;  /* Byte offset in parent dir data  */
    int found;
};

static int remove_callback(const char *name,
                            const struct fat32_dirent *de,
                            uint32_t dir_byte_offset,
                            void *ctx_ptr) {
    struct remove_ctx *ctx = (struct remove_ctx *)ctx_ptr;

    if (fat32_name_cmp_ci(name, ctx->target_name) == 0) {
        ctx->found_cluster      = FAT32_DIRENT_CLUSTER(de);
        ctx->found_attr         = de->DIR_Attr;
        ctx->found_entry_offset = dir_byte_offset;
        ctx->found = 1;
        return 1;  /* stop */
    }
    return 0;
}

static int fat32_vop_remove(struct vnode *dir, const char *name) {
    if (!dir || !name) return -1;
    if (dir->v_type != VDIR) return -1;

    struct fat32_vnode_data *dir_fvd = (struct fat32_vnode_data *)dir->v_data;
    struct fat32_mount *mnt = (struct fat32_mount *)dir->v_mount_data;
    if (!dir_fvd || !mnt) return -1;

    /* Find the entry */
    struct remove_ctx ctx = {
        .target_name      = name,
        .mnt              = mnt,
        .dir_start_cluster = dir_fvd->start_cluster,
        .found = 0,
    };
    fat32_iterate_dir(mnt, dir_fvd->start_cluster, remove_callback, &ctx);

    if (!ctx.found) return -2;  /* ENOENT */

    /* If it's a directory, refuse to delete unless it's empty */
    if (ctx.found_attr & FAT32_ATTR_DIRECTORY) {
        if (ctx.found_cluster >= FAT32_FIRST_DATA_CLUSTER &&
            !fat32_dir_is_empty(mnt, ctx.found_cluster)) {
            uart_puts("[FAT32] remove: directory not empty\n");
            return -4;  /* ENOTEMPTY */
        }
    }

    /* Mark the directory entry as deleted (0xE5) */
    uint32_t clus_size      = mnt->cluster_size;
    uint32_t chain_pos      = ctx.found_entry_offset / clus_size;
    uint32_t off_in_cluster = ctx.found_entry_offset % clus_size;

    uint32_t clus = dir_fvd->start_cluster;
    for (uint32_t i = 0; i < chain_pos; i++) {
        clus = fat32_fat_read(mnt, clus);
        if (clus == 0 || FAT32_IS_EOC(clus)) return -1;
    }

    uint8_t *buf = (uint8_t *)pfa_alloc_frame();
    if (!buf) return -1;

    if (fat32_read_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }

    /* Set first byte of the entry to 0xE5 (deleted marker) */
    buf[off_in_cluster] = FAT32_DIRENT_FREE;

    if (fat32_write_cluster(mnt, clus, buf) != 0) {
        pfa_free_frame((uintptr_t)buf);
        return -1;
    }
    pfa_free_frame((uintptr_t)buf);

    /* Free the file/directory's cluster chain */
    if (ctx.found_cluster >= FAT32_FIRST_DATA_CLUSTER) {
        fat32_free_chain(mnt, ctx.found_cluster);
    }

    uart_puts("[FAT32] Removed '");
    uart_puts(name);
    uart_puts("'\n");

    /* Flush all dirty buffers to disk so changes survive reboot */
    buffer_sync_all(mnt->dev);

    return 0;
}
