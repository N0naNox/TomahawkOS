/**
 * @file init_config.h
 * @brief Init Configuration File Loader
 *
 * Loads and parses /etc/init.conf from the initrd (cpio newc archive) that
 * the bootloader maps into memory and hands to the kernel via Boot_Info.
 * Falls back to VFS lookup if no initrd pointer has been registered.
 *
 * Config format (simple key=value):
 *   # Comment lines start with '#'
 *   key=value
 *   key2=value2
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Path to the init configuration file (used for VFS fallback and messages) */
#define INIT_CFG_PATH          "/etc/init.conf"

/* Limits */
#define INIT_CFG_MAX_ENTRIES   32
#define INIT_CFG_KEY_MAX_LEN   32
#define INIT_CFG_VAL_MAX_LEN   64

/**
 * @brief Register the initrd memory region.
 *
 * Must be called BEFORE init_config_load() so that the loader can find
 * /etc/init.conf inside the cpio archive instead of synthesising it from VFS.
 *
 * @param base  Virtual/physical address of the cpio newc archive in memory.
 * @param size  Size in bytes of the archive.
 */
void init_config_set_initrd(void *base, uint64_t size);

/**
 * @brief Load and parse /etc/init.conf.
 *
 * Primary source: the cpio newc initrd registered via init_config_set_initrd().
 * Fallback: VFS lookup at INIT_CFG_PATH.
 * Calling this more than once re-parses the file (entries are reset).
 *
 * @return 0 on success, -1 if the file could not be read or parsed.
 */
int init_config_load(void);

/**
 * @brief Look up a config value by key.
 *
 * @param key  Null-terminated key string.
 * @return Pointer to the value string, or NULL if not found.
 *         The pointer is valid as long as the module state is live.
 */
const char *init_config_get(const char *key);

/**
 * @brief Check whether init config was successfully loaded.
 *
 * @return 1 if loaded, 0 if not yet loaded or load failed.
 */
int init_config_is_loaded(void);

/**
 * @brief Dump all parsed key=value pairs via vga_write.
 *
 * Intended for the 'initconf' shell command so the user can
 * verify that the file was loaded and parsed correctly.
 */
void init_config_dump(void);

/**
 * @brief Create /etc/init.conf in the VFS from the parsed configuration.
 *
 * Call after init_config_load() so that `ls /etc` and `cat /etc/init.conf`
 * can see the file.  The file is reconstructed from the in-memory table.
 *
 * @return 0 on success, -1 on error.
 */
int init_config_create_vfs_copy(void);
