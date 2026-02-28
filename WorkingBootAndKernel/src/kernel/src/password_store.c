/**
 * @file password_store.c
 * @brief Password hash storage implementation - stores credentials as SHA256 hashes
 *        backed by /etc/shadow on the VFS for persistence across reboots.
 */

#include "include/password_store.h"
#include "include/string.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "uart.h"

/* Define LONESHA256_STATIC so the implementation is included here */
#define LONESHA256_STATIC
#include "include/sha256.h"

/* User entry structure */
typedef struct {
    char username[MAX_USERNAME_LEN];
    uint8_t password_hash[SHA256_HASH_SIZE];
    int used;
} user_entry_t;

/* In-RAM user database */
static user_entry_t user_db[MAX_USERS];
static int user_count = 0;
static int initialized = 0;

/* Helper: compute SHA256 hash of a string */
static void compute_hash(const char* password, uint8_t out_hash[SHA256_HASH_SIZE]) {
    size_t len = strlen(password);
    lonesha256(out_hash, (const unsigned char*)password, len);
}

/* Helper: compare two hashes */
static int hash_compare(const uint8_t* h1, const uint8_t* h2) {
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        if (h1[i] != h2[i]) return -1;
    }
    return 0;
}

/* Helper: find user by username, returns index or -1 if not found */
static int find_user(const char* username) {
    for (int i = 0; i < user_count; i++) {
        if (user_db[i].used) {
            /* Simple string compare */
            const char* a = username;
            const char* b = user_db[i].username;
            int match = 1;
            while (*a && *b) {
                if (*a != *b) { match = 0; break; }
                a++; b++;
            }
            if (match && *a == *b) return i;
        }
    }
    return -1;
}

/* ---- Hex encoding / decoding helpers ---- */

static const char hex_chars[] = "0123456789abcdef";

/* Encode raw hash bytes to a 64-char hex string (no null terminator added) */
static void hash_to_hex(const uint8_t hash[SHA256_HASH_SIZE], char *out) {
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        out[i * 2]     = hex_chars[(hash[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[ hash[i]       & 0xF];
    }
}

/* Decode a single hex digit, returns 0-15 or -1 on error */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode a 64-char hex string into 32 raw bytes. Returns 0 on success, -1 on error */
static int hex_to_hash(const char *hex, uint8_t out[SHA256_HASH_SIZE]) {
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        int hi = hex_digit(hex[i * 2]);
        int lo = hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* ---- VFS-backed persistence ---- */

#define SHADOW_PATH "/mnt/fat/SHADOW.DAT"

/* Save all current users to the persistent FAT32 shadow file */
static void save_shadow(void) {
    /* Build the file content in a static buffer */
    static char buf[8192];
    int pos = 0;

    /* Write each used entry */
    for (int i = 0; i < MAX_USERS && pos < (int)sizeof(buf) - 128; i++) {
        if (!user_db[i].used) continue;

        /* username */
        for (int j = 0; user_db[i].username[j]; j++)
            buf[pos++] = user_db[i].username[j];

        buf[pos++] = ':';

        /* hex hash */
        char hex[SHA256_HASH_SIZE * 2];
        hash_to_hex(user_db[i].password_hash, hex);
        for (int j = 0; j < SHA256_HASH_SIZE * 2; j++)
            buf[pos++] = hex[j];

        buf[pos++] = '\n';
    }

    /* Write to persistent FAT32 file */
    struct vnode *vp = NULL;
    if (vfs_resolve_path(SHADOW_PATH, &vp) != 0 || !vp) {
        /* File doesn't exist yet — create it */
        struct vnode *parent = NULL;
        if (vfs_resolve_path("/mnt/fat", &parent) == 0 && parent) {
            vfs_create(parent, "SHADOW.DAT", &vp);
        }
    }
    if (vp) {
        vfs_write_at(vp, buf, (size_t)pos, 0);
        vfs_close(vp);
        uart_puts("[PASSWORD_STORE] Saved shadow to FAT32 (");
        uart_putu((unsigned)pos);
        uart_puts(" bytes)\n");
    } else {
        uart_puts("[PASSWORD_STORE] WARNING: Could not save to FAT32\n");
    }

    /* Also update the in-memory ramfs /etc/shadow copy */
    struct vnode *ramfs_vp = NULL;
    if (vfs_resolve_path("/etc/shadow", &ramfs_vp) == 0 && ramfs_vp) {
        vfs_write_at(ramfs_vp, buf, (size_t)pos, 0);
        vfs_close(ramfs_vp);
    }
}

/* Load users from persistent FAT32 shadow file.
 * Called once after the FAT32 volume is mounted. */
void password_store_load_shadow(void) {
    struct vnode *vp = NULL;

    /* Try persistent FAT32 first */
    if (vfs_resolve_path(SHADOW_PATH, &vp) != 0 || !vp) {
        /* Fall back to ramfs /etc/shadow */
        uart_puts("[PASSWORD_STORE] /mnt/fat/SHADOW.DAT not found, trying /etc/shadow\n");
        if (vfs_resolve_path("/etc/shadow", &vp) != 0 || !vp) {
            uart_puts("[PASSWORD_STORE] No shadow file found, keeping defaults\n");
            return;
        }
    }

    int64_t fsize = vfs_getsize(vp);
    if (fsize <= 0) {
        uart_puts("[PASSWORD_STORE] /etc/shadow is empty, keeping defaults\n");
        vfs_close(vp);
        return;
    }
    if (fsize > 8192) fsize = 8192;  /* sanity cap */

    static char buf[8192];
    int nread = vfs_read_at(vp, buf, (size_t)fsize, 0);
    vfs_close(vp);
    if (nread <= 0) {
        uart_puts("[PASSWORD_STORE] Failed to read /etc/shadow\n");
        return;
    }

    /* Clear existing DB — we rebuild entirely from the file */
    for (int i = 0; i < MAX_USERS; i++) {
        user_db[i].used = 0;
        for (int j = 0; j < MAX_USERNAME_LEN; j++)
            user_db[i].username[j] = 0;
    }
    user_count = 0;

    /* Parse line by line */
    int loaded = 0;
    int pos = 0;
    while (pos < nread && user_count < MAX_USERS) {
        /* Skip to start of line */
        if (buf[pos] == '\n' || buf[pos] == '\r') { pos++; continue; }

        /* Find end of line */
        int line_start = pos;
        while (pos < nread && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        int line_end = pos;

        /* Skip comment lines */
        if (buf[line_start] == '#') continue;

        /* Find the colon separator */
        int colon = -1;
        for (int i = line_start; i < line_end; i++) {
            if (buf[i] == ':') { colon = i; break; }
        }
        if (colon < 0) continue;  /* malformed line */

        int name_len = colon - line_start;
        int hash_len = line_end - colon - 1;

        /* Skip entries with no password (e.g. "guest:*") */
        if (hash_len == 1 && buf[colon + 1] == '*') continue;

        /* Hash must be exactly 64 hex chars */
        if (hash_len != SHA256_HASH_SIZE * 2) continue;
        if (name_len <= 0 || name_len >= MAX_USERNAME_LEN) continue;

        /* Decode the hash */
        uint8_t hash[SHA256_HASH_SIZE];
        if (hex_to_hash(&buf[colon + 1], hash) != 0) continue;

        /* Add to user_db */
        int slot = user_count;
        for (int j = 0; j < name_len; j++)
            user_db[slot].username[j] = buf[line_start + j];
        user_db[slot].username[name_len] = '\0';

        for (int j = 0; j < SHA256_HASH_SIZE; j++)
            user_db[slot].password_hash[j] = hash[j];

        user_db[slot].used = 1;
        user_count++;
        loaded++;
    }

    uart_puts("[PASSWORD_STORE] Loaded ");
    uart_putu((unsigned)loaded);
    uart_puts(" users from /etc/shadow\n");
}

void password_store_init(void) {
    if (initialized) return;
    
    /* Clear the database */
    for (int i = 0; i < MAX_USERS; i++) {
        user_db[i].used = 0;
        for (int j = 0; j < MAX_USERNAME_LEN; j++) {
            user_db[i].username[j] = 0;
        }
    }
    user_count = 0;
    
    /* Add default admin user with password "1234" */
    /* SHA256("1234") = 03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4 */
    /* But we'll compute it properly */
    const char* admin_name = "admin";
    const char* admin_pass = "1234";
    
    /* Copy username */
    int i = 0;
    for (; i < MAX_USERNAME_LEN - 1 && admin_name[i]; i++) {
        user_db[0].username[i] = admin_name[i];
    }
    user_db[0].username[i] = '\0';
    
    /* Compute and store hash */
    compute_hash(admin_pass, user_db[0].password_hash);
    user_db[0].used = 1;
    user_count = 1;
    
    initialized = 1;
    
    uart_puts("[PASSWORD_STORE] Initialized with default admin user\n");
}

int password_store_add(const char* username, const char* password) {
    if (!username || !password) return -1;
    if (!initialized) password_store_init();
    
    /* Check if user already exists */
    int idx = find_user(username);
    
    if (idx >= 0) {
        /* Update existing user's password */
        compute_hash(password, user_db[idx].password_hash);
        uart_puts("[PASSWORD_STORE] Updated password for user: ");
        uart_puts(username);
        uart_puts("\n");
        save_shadow();
        return 0;
    }
    
    /* Add new user */
    if (user_count >= MAX_USERS) {
        uart_puts("[PASSWORD_STORE] Error: store is full\n");
        return -1;
    }
    
    /* Find first unused slot */
    for (int i = 0; i < MAX_USERS; i++) {
        if (!user_db[i].used) {
            /* Copy username */
            int j = 0;
            for (; j < MAX_USERNAME_LEN - 1 && username[j]; j++) {
                user_db[i].username[j] = username[j];
            }
            user_db[i].username[j] = '\0';
            
            /* Compute and store hash */
            compute_hash(password, user_db[i].password_hash);
            user_db[i].used = 1;
            user_count++;
            
            uart_puts("[PASSWORD_STORE] Added new user: ");
            uart_puts(username);
            uart_puts("\n");
            save_shadow();
            return 0;
        }
    }
    
    return -1;
}

int password_store_verify(const char* username, const char* password) {
    if (!username || !password) return -1;
    if (!initialized) password_store_init();
    
    int idx = find_user(username);
    if (idx < 0) {
        uart_puts("[PASSWORD_STORE] User not found: ");
        uart_puts(username);
        uart_puts("\n");
        return -1;
    }
    
    /* Compute hash of provided password */
    uint8_t computed_hash[SHA256_HASH_SIZE];
    compute_hash(password, computed_hash);
    
    /* Compare with stored hash */
    if (hash_compare(computed_hash, user_db[idx].password_hash) == 0) {
        uart_puts("[PASSWORD_STORE] Login successful for: ");
        uart_puts(username);
        uart_puts("\n");
        return 0;
    }
    
    uart_puts("[PASSWORD_STORE] Invalid password for: ");
    uart_puts(username);
    uart_puts("\n");
    return -1;
}

int password_store_user_exists(const char* username) {
    if (!username) return 0;
    if (!initialized) password_store_init();
    return find_user(username) >= 0 ? 1 : 0;
}

int password_store_get_count(void) {
    if (!initialized) password_store_init();
    return user_count;
}

int password_store_get_uid(const char* username) {
    if (!username) return -1;
    if (!initialized) password_store_init();
    return find_user(username);
}

int password_store_get_username(int uid, char* buf, int buf_size) {
    if (!buf || buf_size <= 0) return -1;
    if (!initialized) password_store_init();
    if (uid < 0 || uid >= MAX_USERS || !user_db[uid].used) {
        buf[0] = '\0';
        return -1;
    }
    
    /* Copy username to buffer */
    int i = 0;
    for (; i < buf_size - 1 && user_db[uid].username[i]; i++) {
        buf[i] = user_db[uid].username[i];
    }
    buf[i] = '\0';
    return 0;
}
