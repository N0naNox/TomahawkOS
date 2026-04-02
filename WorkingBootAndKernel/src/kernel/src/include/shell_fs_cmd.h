/**
 * @file shell_fs_cmd.h
 * @brief Kernel-side filesystem commands for the Tomahawk Shell
 */

#ifndef SHELL_FS_CMD_H
#define SHELL_FS_CMD_H

/**
 * @brief Dispatch a shell command to the appropriate fs handler
 * @param cmdline Full command line string (e.g. "ls /etc")
 * @return 0 if command was recognized and handled, -1 if unknown
 */
int shell_fs_dispatch(const char *cmdline);

#endif /* SHELL_FS_CMD_H */
