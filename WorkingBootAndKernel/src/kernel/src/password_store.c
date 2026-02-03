/**
 * @file password_store.c
 * @brief Password hash storage implementation - stores credentials as SHA256 hashes in RAM
 */

#include "include/password_store.h"
#include "include/string.h"
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
