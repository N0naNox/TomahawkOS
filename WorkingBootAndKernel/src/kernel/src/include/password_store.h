/**
 * @file password_store.h
 * @brief Password hash storage - stores user credentials as SHA256 hashes in RAM
 */

#ifndef PASSWORD_STORE_H
#define PASSWORD_STORE_H

#include <stdint.h>

#define MAX_USERS 64
#define MAX_USERNAME_LEN 32
#define SHA256_HASH_SIZE 32

/**
 * @brief Initialize the password store with a default admin account
 * Default credentials: admin / 1234
 */
void password_store_init(void);

/**
 * @brief Load users from /etc/shadow (VFS).
 * Call after filesystem is mounted to pick up persisted registrations.
 */
void password_store_load_shadow(void);

/**
 * @brief Store a new user or update existing user's password hash
 * @param username The username (max 31 chars + null terminator)
 * @param password The plaintext password to hash and store
 * @return 0 on success, -1 on failure (store full or invalid input)
 */
int password_store_add(const char* username, const char* password);

/**
 * @brief Verify a user's password
 * @param username The username to verify
 * @param password The plaintext password to check
 * @return 0 if credentials are valid, -1 if invalid
 */
int password_store_verify(const char* username, const char* password);

/**
 * @brief Check if a user exists
 * @param username The username to check
 * @return 1 if user exists, 0 otherwise
 */
int password_store_user_exists(const char* username);

/**
 * @brief Get the number of stored users
 * @return The count of users in the store
 */
int password_store_get_count(void);

/**
 * @brief Get user ID (index) for a username
 * @param username The username to look up
 * @return User ID (0+) if found, -1 if not found
 */
int password_store_get_uid(const char* username);

/**
 * @brief Get username for a user ID
 * @param uid The user ID
 * @param buf Buffer to store username
 * @param buf_size Size of buffer
 * @return 0 on success, -1 on failure
 */
int password_store_get_username(int uid, char* buf, int buf_size);

/**
 * @brief Delete a user by UID (cannot delete uid 0/admin)
 * @param uid The user ID to delete
 * @return 0 on success, -1 on failure
 */
int password_store_delete(int uid);

/**
 * @brief Change a user's password after verifying the old one
 * @param uid The user ID
 * @param old_password Current password (must match)
 * @param new_password New password to set
 * @return 0 on success, -1 if old_password wrong or uid invalid
 */
int password_store_change_password(int uid, const char* old_password, const char* new_password);

/**
 * @brief Check if a user has admin privileges
 * @param uid The user ID (uid 0 always returns 1)
 * @return 1 if admin, 0 otherwise
 */
int password_store_is_admin(int uid);

/**
 * @brief Set or clear admin flag for a user
 * @param uid The user ID (uid 0 cannot be changed)
 * @param value 1 to grant admin, 0 to revoke
 * @return 0 on success, -1 on failure
 */
int password_store_set_admin(int uid, int value);

#endif /* PASSWORD_STORE_H */
