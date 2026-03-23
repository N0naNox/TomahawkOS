/**
 * @file init_config.c
 * @brief Init Configuration File Loader Implementation
 *
 * Primary source: scans the cpio newc initrd (registered via
 * init_config_set_initrd) to find ./etc/init.conf and parses it
 * line-by-line as key=value pairs.
 *
 * Fallback: if no initrd has been registered, looks up the file in the
 * in-memory VFS (legacy behaviour kept for compatibility).
 */

#include "include/init_config.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/uart.h"
#include <stddef.h>
#include <stdint.h>

/* ========== Internal state ========== */

typedef struct {
    char key[INIT_CFG_KEY_MAX_LEN];
    char val[INIT_CFG_VAL_MAX_LEN];
} cfg_entry_t;

static cfg_entry_t cfg_table[INIT_CFG_MAX_ENTRIES];
static int         cfg_count   = 0;
static int         cfg_loaded  = 0;  /* 1 if last load succeeded */

/* Initrd region registered by the kernel at boot */
static void    *s_initrd_base = NULL;
static uint64_t s_initrd_size = 0;

/* ========== Private helpers ========== */

/* Copy at most n chars from src to dst, always NUL-terminate. */
static void safe_copy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    while (i < n - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Trim leading/trailing whitespace in-place; returns pointer to first non-space. */
static char *trim(char *s) {
    /* Leading */
    while (*s == ' ' || *s == '\t') s++;

    /* Trailing */
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'
                    || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

/* Parse one line into the table.  Returns 1 if a valid entry was added. */
static int parse_line(char *line) {
    char *p = trim(line);

    /* Skip blank lines and comments */
    if (*p == '\0' || *p == '#') {
        return 0;
    }

    /* Find '=' separator */
    char *eq = p;
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') {
        uart_puts("[INITCFG] Skipping malformed line (no '=')\n");
        return 0;
    }

    /* NUL-terminate the key part */
    *eq = '\0';
    char *key = trim(p);
    char *val = trim(eq + 1);

    if (*key == '\0') {
        return 0;  /* Empty key — skip */
    }

    if (cfg_count >= INIT_CFG_MAX_ENTRIES) {
        uart_puts("[INITCFG] WARNING: max entries reached, skipping '");
        uart_puts(key);
        uart_puts("'\n");
        return 0;
    }

    safe_copy(cfg_table[cfg_count].key, key, INIT_CFG_KEY_MAX_LEN);
    safe_copy(cfg_table[cfg_count].val, val, INIT_CFG_VAL_MAX_LEN);
    cfg_count++;
    return 1;
}

/* Parse a buffer of text (not NUL-terminated; length given).
 * Returns the number of key=value entries added. */
static int parse_buffer(const char *buf, int n) {
    char  line[128];
    int   line_len = 0;
    int   entries  = 0;

    for (int i = 0; i <= n; i++) {
        char c = (i < n) ? buf[i] : '\0';
        if (c == '\n' || c == '\r' || c == '\0') {
            line[line_len] = '\0';
            if (line_len > 0) {
                entries += parse_line(line);
            }
            line_len = 0;
        } else {
            if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = c;
            }
        }
    }
    return entries;
}

/* ========== cpio newc scanner ========== */

/* The cpio "new ASCII" (newc) header is exactly 110 bytes:
 *   magic[6]  "070701"
 *   13 fields × 8 hex ASCII chars = 104 bytes
 * Field offsets (0-based from start of header):
 *   54 – c_filesize  (8 hex chars)
 *   94 – c_namesize  (8 hex chars, includes NUL terminator)
 * After the header comes:
 *   filename  (namesize bytes)
 *   padding   to align (110 + namesize) up to a 4-byte boundary
 *   file data (filesize bytes)
 *   padding   to align data end up to a 4-byte boundary
 */
#define CPIO_HDR_SIZE 110

static uint32_t parse_hex8(const uint8_t *s) {
    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t d;
        uint8_t c = s[i];
        if (c >= '0' && c <= '9')      d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else                            d = 0;
        val = (val << 4) | d;
    }
    return val;
}

/* Scan the cpio archive in [base, base+size) for a file whose path matches
 * target_name (with or without a leading "./").
 * Returns a pointer to the raw file data and sets *out_size to its length.
 * Returns NULL if not found. */
static const char *cpio_find_file(const void *base, uint64_t size,
                                  const char *target_name,
                                  uint32_t *out_size)
{
    const uint8_t *p   = (const uint8_t *)base;
    const uint8_t *end = p + size;

    /* Strip leading "./" from target so we can compare bare names */
    const char *bare_target = target_name;
    if (bare_target[0] == '.' && bare_target[1] == '/') {
        bare_target += 2;
    }

    while (p + CPIO_HDR_SIZE <= end) {
        /* Validate "070701" magic */
        if (p[0] != '0' || p[1] != '7' || p[2] != '0' ||
            p[3] != '7' || p[4] != '0' || p[5] != '1') {
            break;
        }

        uint32_t filesize = parse_hex8(p + 54);
        uint32_t namesize = parse_hex8(p + 94);

        if (namesize == 0) break; /* Corrupt archive */

        const char *name = (const char *)(p + CPIO_HDR_SIZE);

        /* Trailer entry signals end of archive */
        if (namesize == 11 && strcmp(name, "TRAILER!!!") == 0) {
            break;
        }

        /* data_offset is (header + namesize) rounded up to 4 */
        uint32_t data_offset = ((uint32_t)CPIO_HDR_SIZE + namesize + 3u) & ~3u;

        /* Match: accept "./foo/bar" or "foo/bar" against bare_target */
        const char *bare_name = name;
        if (bare_name[0] == '.' && bare_name[1] == '/') {
            bare_name += 2;
        }

        if (strcmp(bare_name, bare_target) == 0 && filesize > 0) {
            if (out_size) *out_size = filesize;
            return (const char *)(p + data_offset);
        }

        /* Advance to next header */
        uint32_t next_offset = data_offset + ((filesize + 3u) & ~3u);
        p += next_offset;
    }

    return NULL;
}

/* ========== Public API ========== */

void init_config_set_initrd(void *base, uint64_t size) {
    s_initrd_base = base;
    s_initrd_size = size;
    uart_puts("[INITCFG] Initrd registered: ");
    /* Print size (simple manual itoa) */
    char buf[20];
    int_to_str((int)size, buf, 10);
    uart_puts(buf);
    uart_puts(" bytes\n");
}

int init_config_load(void) {
    uart_puts("[INITCFG] Loading " INIT_CFG_PATH " ...\n");

    /* Reset state */
    cfg_count  = 0;
    cfg_loaded = 0;
    for (int i = 0; i < INIT_CFG_MAX_ENTRIES; i++) {
        cfg_table[i].key[0] = '\0';
        cfg_table[i].val[0] = '\0';
    }

    int entries_added = 0;

    /* ---- Primary: scan initrd cpio archive ---- */
    if (s_initrd_base != NULL && s_initrd_size > 0) {
        uart_puts("[INITCFG] Scanning initrd for " INIT_CFG_PATH "\n");

        uint32_t file_size = 0;
        const char *file_data = cpio_find_file(s_initrd_base, s_initrd_size,
                                               "etc/init.conf", &file_size);
        if (file_data != NULL && file_size > 0) {
            uart_puts("[INITCFG] Found in initrd — parsing\n");

            /* Cap to avoid runaway reads */
            if (file_size > 4095) file_size = 4095;

            entries_added = parse_buffer(file_data, (int)file_size);
            cfg_loaded = 1;

            uart_puts("[INITCFG] Parsed ");
            char countbuf[8];
            int_to_str(entries_added, countbuf, 10);
            uart_puts(countbuf);
            uart_puts(entries_added == 1 ? " entry" : " entries");
            uart_puts(" from initrd:" INIT_CFG_PATH "\n");
            return 0;
        }

        uart_puts("[INITCFG] WARNING: " INIT_CFG_PATH " not found in initrd\n");
    }

    /* ---- Fallback: resolve via VFS ---- */
    uart_puts("[INITCFG] Falling back to VFS lookup\n");

    struct vnode *vp = vfs_resolve_path_ramfs(INIT_CFG_PATH);
    if (!vp) {
        uart_puts("[INITCFG] ERROR: " INIT_CFG_PATH " not found in VFS\n");
        return -1;
    }

    struct inode *ip = (struct inode *)vp->v_data;
    if (!ip || ip->i_size == 0) {
        uart_puts("[INITCFG] WARNING: " INIT_CFG_PATH " is empty\n");
        cfg_loaded = 1;
        return 0;
    }

    /* Read the whole file (capped at 4095 bytes for safety) */
    char buf[4096];
    size_t to_read = ip->i_size;
    if (to_read > sizeof(buf) - 1) {
        to_read = sizeof(buf) - 1;
    }

    int n = vfs_read(vp, buf, to_read);
    if (n <= 0) {
        uart_puts("[INITCFG] ERROR: Failed to read " INIT_CFG_PATH "\n");
        return -1;
    }
    buf[n] = '\0';

    entries_added = parse_buffer(buf, n);
    cfg_loaded = 1;

    uart_puts("[INITCFG] Parsed ");
    char countbuf[8];
    int_to_str(entries_added, countbuf, 10);
    uart_puts(countbuf);
    uart_puts(entries_added == 1 ? " entry" : " entries");
    uart_puts(" from VFS:" INIT_CFG_PATH "\n");

    return 0;
}

const char *init_config_get(const char *key) {
    if (!key || !cfg_loaded) return NULL;

    for (int i = 0; i < cfg_count; i++) {
        if (strcmp(cfg_table[i].key, key) == 0) {
            return cfg_table[i].val;
        }
    }
    return NULL;
}

int init_config_is_loaded(void) {
    return cfg_loaded;
}

int init_config_create_vfs_copy(void) {
    if (!cfg_loaded || cfg_count == 0) {
        uart_puts("[INITCFG] Nothing to copy to VFS (not loaded or empty)\n");
        return -1;
    }

    /* Resolve /etc */
    struct vnode *etc = vfs_resolve_path_ramfs("/etc");
    if (!etc) {
        uart_puts("[INITCFG] /etc not found in VFS\n");
        return -1;
    }

    /* If the file already exists, skip */
    if (vfs_lookup_ramfs(etc, "init.conf") != NULL) {
        uart_puts("[INITCFG] /etc/init.conf already exists in VFS\n");
        return 0;
    }

    /* Reconstruct file content from parsed entries */
    char buf[2048];
    int off = 0;

    /* Header comment */
    const char *hdr = "# TomahawkOS Init Configuration\n";
    for (int i = 0; hdr[i] && off < (int)sizeof(buf) - 1; i++)
        buf[off++] = hdr[i];

    for (int i = 0; i < cfg_count && off < (int)sizeof(buf) - 4; i++) {
        /* key=value\n */
        for (const char *s = cfg_table[i].key; *s && off < (int)sizeof(buf) - 3; s++)
            buf[off++] = *s;
        buf[off++] = '=';
        for (const char *s = cfg_table[i].val; *s && off < (int)sizeof(buf) - 2; s++)
            buf[off++] = *s;
        buf[off++] = '\n';
    }
    buf[off] = '\0';

    /* Create the file */
    struct vnode *f = vfs_create_file(etc, "init.conf");
    if (!f) {
        uart_puts("[INITCFG] Failed to create /etc/init.conf in VFS\n");
        return -1;
    }
    vfs_write(f, buf, (size_t)off);
    uart_puts("[INITCFG] Created /etc/init.conf in VFS (visible to ls/cat)\n");
    return 0;
}

void init_config_dump(void) {
    if (!cfg_loaded) {
        vga_write("[initconf] Init configuration has NOT been loaded yet.\n");
        return;
    }

    vga_write("=== Init Configuration (" INIT_CFG_PATH ") ===\n");

    if (cfg_count == 0) {
        vga_write("  (empty — no key=value pairs found)\n");
    } else {
        for (int i = 0; i < cfg_count; i++) {
            vga_write("  ");
            vga_write(cfg_table[i].key);
            vga_write(" = ");
            vga_write(cfg_table[i].val);
            vga_write("\n");
        }
    }

    /* Show entry count */
    char countbuf[8];
    int_to_str(cfg_count, countbuf, 10);
    vga_write("--- ");
    vga_write(countbuf);
    vga_write(cfg_count == 1 ? " entry" : " entries");
    vga_write(" loaded ---\n");
}

int init_config_set(const char *key, const char *val) {
    if (!key || !val) return -1;
    /* Update existing entry */
    for (int i = 0; i < cfg_count; i++) {
        if (strcmp(cfg_table[i].key, key) == 0) {
            safe_copy(cfg_table[i].val, val, INIT_CFG_VAL_MAX_LEN);
            return 0;
        }
    }
    /* Add new entry */
    if (cfg_count >= INIT_CFG_MAX_ENTRIES) return -1;
    safe_copy(cfg_table[cfg_count].key, key, INIT_CFG_KEY_MAX_LEN);
    safe_copy(cfg_table[cfg_count].val, val, INIT_CFG_VAL_MAX_LEN);
    cfg_count++;
    cfg_loaded = 1;
    return 0;
}

int init_config_get_count(void) {
    return cfg_count;
}

int init_config_get_entry(int index, const char **key, const char **val) {
    if (index < 0 || index >= cfg_count) return -1;
    if (key) *key = cfg_table[index].key;
    if (val) *val = cfg_table[index].val;
    return 0;
}

int init_config_build_buffer(char *buf, int bufsz) {
    int off = 0;
    const char *hdr = "# TomahawkOS Init Configuration\n";
    for (int i = 0; hdr[i] && off < bufsz - 1; i++)
        buf[off++] = hdr[i];
    for (int i = 0; i < cfg_count && off < bufsz - 4; i++) {
        for (const char *s = cfg_table[i].key; *s && off < bufsz - 3; s++)
            buf[off++] = *s;
        buf[off++] = '=';
        for (const char *s = cfg_table[i].val; *s && off < bufsz - 2; s++)
            buf[off++] = *s;
        buf[off++] = '\n';
    }
    buf[off] = '\0';
    return off;
}
