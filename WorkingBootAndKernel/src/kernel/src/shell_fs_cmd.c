/**
 * @file shell_fs_cmd.c
 * @brief Kernel-side filesystem commands for the Tomahawk Shell
 *
 * Implements: ls, cat, mkdir, touch, write, pwd, tree
 * Called from SYS_SHELL_CMD syscall - the full command string is passed in.
 */

#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "include/mount.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/shell_fs_cmd.h"
#include "include/demos.h"
#include "include/init_config.h"
#include <uart.h>
#include <stddef.h>

/* ========== Helpers ========== */

/* Skip leading whitespace, return pointer to first non-space */
static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Copy the next whitespace-delimited token into buf, return pointer past it */
static const char *next_token(const char *s, char *buf, int bufsize) {
    s = skip_spaces(s);
    int i = 0;
    while (*s && *s != ' ' && *s != '\t' && i < bufsize - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* Print an integer to VGA */
static void vga_print_int(int val) {
    char num[16];
    int_to_str(val, num, 10);
    vga_write(num);
}

/* ========== ls ========== */

static void cmd_ls(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') path = "/";

    struct vnode *vp = vfs_resolve_path(path);
    if (!vp) {
        vga_write("ls: cannot access '");
        vga_write(path);
        vga_write("': No such file or directory\n");
        return;
    }

    if (vp->v_type != VDIR) {
        /* It's a file - just show its name and size */
        struct inode *ip = (struct inode *)vp->v_data;
        vga_write("-  ");
        vga_print_int(ip ? ip->i_size : 0);
        vga_write("  ");
        /* Extract basename from path */
        const char *base = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/' && *(p+1)) base = p + 1;
        }
        vga_write(base);
        vga_write("\n");
        return;
    }

    /* List directory children */
    if (vp->v_nchildren == 0) {
        vga_write("(empty directory)\n");
        return;
    }

    /* Column header */
    vga_write("Type  Size      Name\n");
    vga_write("----  --------  ----------------\n");

    struct dir_entry *de = vp->v_children;
    while (de) {
        struct vnode *child = de->vnode;
        if (child->v_type == VDIR) {
            vga_write("d     -         ");
        } else {
            vga_write("-     ");
            struct inode *ip = (struct inode *)child->v_data;
            int sz = ip ? (int)ip->i_size : 0;
            char szbuf[12];
            int_to_str(sz, szbuf, 10);
            vga_write(szbuf);
            /* Pad to 10 chars */
            int pad = 10 - (int)strlen(szbuf);
            while (pad-- > 0) vga_write(" ");
        }
        vga_write(de->name);
        vga_write("\n");
        de = de->next;
    }
}

/* ========== cat ========== */

static void cmd_cat(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') {
        vga_write("cat: missing file operand\n");
        return;
    }

    struct vnode *vp = vfs_resolve_path(path);
    if (!vp) {
        vga_write("cat: ");
        vga_write(path);
        vga_write(": No such file or directory\n");
        return;
    }
    if (vp->v_type == VDIR) {
        vga_write("cat: ");
        vga_write(path);
        vga_write(": Is a directory\n");
        return;
    }

    struct inode *ip = (struct inode *)vp->v_data;
    if (!ip || ip->i_size == 0) {
        /* Empty file */
        return;
    }

    /* Read and print file contents (up to 4096 bytes) */
    char buf[4097];
    size_t to_read = ip->i_size;
    if (to_read > 4096) to_read = 4096;

    int n = vfs_read(vp, buf, to_read);
    if (n > 0) {
        buf[n] = '\0';
        vga_write(buf);
        if (buf[n-1] != '\n') 
            vga_write("\n");
    }
}

/* ========== mkdir ========== */

static void cmd_mkdir(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') {
        vga_write("mkdir: missing operand\n");
        return;
    }

    /* Split into parent path and new dir name */
    /* Find last '/' */
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    struct vnode *parent;
    const char *name;

    if (!last_slash || last_slash == path) {
        /* No slash or slash at start -> parent is root */
        parent = vfs_get_root();
        name = (last_slash == path) ? path + 1 : path;
    } else {
        /* Resolve parent path */
        char parent_path[256];
        int plen = (int)(last_slash - path);
        if (plen >= 256) plen = 255;
        for (int i = 0; i < plen; i++) parent_path[i] = path[i];
        parent_path[plen] = '\0';
        parent = vfs_resolve_path(parent_path);
        name = last_slash + 1;
    }

    if (!parent) {
        vga_write("mkdir: cannot create '");
        vga_write(path);
        vga_write("': Parent directory not found\n");
        return;
    }
    if (*name == '\0') {
        vga_write("mkdir: missing directory name\n");
        return;
    }

    struct vnode *dir = vfs_mkdir(parent, name);
    if (!dir) {
        vga_write("mkdir: cannot create '");
        vga_write(path);
        vga_write("': Already exists or error\n");
    }
}

/* ========== touch ========== */

static void cmd_touch(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') {
        vga_write("touch: missing file operand\n");
        return;
    }

    /* Check if file already exists */
    struct vnode *existing = vfs_resolve_path(path);
    if (existing) {
        /* File already exists - touch is a no-op */
        return;
    }

    /* Split parent/name like mkdir */
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    struct vnode *parent;
    const char *name;

    if (!last_slash || last_slash == path) {
        parent = vfs_get_root();
        name = (last_slash == path) ? path + 1 : path;
    } else {
        char parent_path[256];
        int plen = (int)(last_slash - path);
        if (plen >= 256) plen = 255;
        for (int i = 0; i < plen; i++) parent_path[i] = path[i];
        parent_path[plen] = '\0';
        parent = vfs_resolve_path(parent_path);
        name = last_slash + 1;
    }

    if (!parent) {
        vga_write("touch: cannot create '");
        vga_write(path);
        vga_write("': Parent directory not found\n");
        return;
    }

    struct vnode *f = vfs_create_file(parent, name);
    if (!f) {
        vga_write("touch: cannot create '");
        vga_write(path);
        vga_write("'\n");
    }
}

/* ========== write (write <path> <text>) ========== */

static void cmd_write(const char *args) {
    char path[256];
    const char *rest = next_token(args, path, 256);
    if (path[0] == '\0') {
        vga_write("write: usage: write <path> <text>\n");
        return;
    }

    const char *text = skip_spaces(rest);
    if (*text == '\0') {
        vga_write("write: missing text argument\n");
        return;
    }

    /* Resolve or create the file */
    struct vnode *vp = vfs_resolve_path(path);
    if (!vp) {
        /* Try to create it */
        cmd_touch(path);
        vp = vfs_resolve_path(path);
    }
    if (!vp) {
        vga_write("write: cannot open '");
        vga_write(path);
        vga_write("'\n");
        return;
    }
    if (vp->v_type == VDIR) {
        vga_write("write: '");
        vga_write(path);
        vga_write("' is a directory\n");
        return;
    }

    size_t len = strlen(text);
    int written = vfs_write(vp, text, len);
    if (written < 0) {
        vga_write("write: error writing to file\n");
    } else {
        vga_write("Wrote ");
        vga_print_int(written);
        vga_write(" bytes to ");
        vga_write(path);
        vga_write("\n");
    }
}

/* ========== pwd ========== */

static void cmd_pwd(const char *args) {
    (void)args;
    /* We don't track a current working directory yet, so always show "/" */
    vga_write("/\n");
}

/* ========== tree (recursive directory listing) ========== */

static void tree_recursive(struct vnode *vp, int depth) {
    struct dir_entry *de = vp->v_children;
    while (de) {
        for (int i = 0; i < depth; i++) vga_write("  ");
        if (de->vnode->v_type == VDIR) {
            vga_write(de->name);
            vga_write("/\n");
            tree_recursive(de->vnode, depth + 1);
        } else {
            vga_write(de->name);
            vga_write("\n");
        }
        de = de->next;
    }
}

static void cmd_tree(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') path = "/";

    struct vnode *vp = vfs_resolve_path(path);
    if (!vp) {
        vga_write("tree: '");
        vga_write(path);
        vga_write("': not found\n");
        return;
    }
    if (vp->v_type != VDIR) {
        vga_write(path);
        vga_write("\n");
        return;
    }

    vga_write(path);
    vga_write("\n");
    tree_recursive(vp, 1);
}

/* ========== stat ========== */

static void cmd_stat(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') {
        vga_write("stat: missing file operand\n");
        return;
    }

    struct vnode *vp = vfs_resolve_path(path);
    if (!vp) {
        vga_write("stat: '");
        vga_write(path);
        vga_write("': No such file or directory\n");
        return;
    }

    vga_write("  File: ");
    vga_write(path);
    vga_write("\n");

    vga_write("  Type: ");
    switch (vp->v_type) {
        case VDIR: vga_write("directory\n"); break;
        case VREG: vga_write("regular file\n"); break;
        case VCHR: vga_write("character device\n"); break;
        case VBLK: vga_write("block device\n"); break;
        default:   vga_write("unknown\n"); break;
    }

    struct inode *ip = (struct inode *)vp->v_data;
    if (ip) {
        vga_write("  Size: ");
        vga_print_int((int)ip->i_size);
        vga_write(" bytes\n");

        vga_write("  Inode: ");
        vga_print_int((int)ip->i_no);
        vga_write("\n");

        if (vp->v_type == VDIR) {
            vga_write("  Children: ");
            vga_print_int(vp->v_nchildren);
            vga_write("\n");
        }
    }
}

/* ========== initconf ========== */

static void cmd_initconf(const char *args) {
    (void)args;

    if (!init_config_is_loaded()) {
        /* Attempt a (re-)load so the user gets useful feedback */
        vga_write("[initconf] Config not loaded yet - attempting load...\n");
        if (init_config_load() != 0) {
            vga_write("[initconf] ERROR: Failed to load " INIT_CFG_PATH "\n");
            return;
        }
    }

    init_config_dump();
}

/* ========== Main dispatcher ========== */

/**
 * @brief Handle a shell command string from userspace
 * @param cmdline The full command line (e.g. "ls /etc")
 * @return 0 if command was handled, -1 if unknown
 */
int shell_fs_dispatch(const char *cmdline) {
    if (!cmdline) return -1;

    char cmd[64];
    const char *args = next_token(cmdline, cmd, 64);

    /* Check for empty command */
    if (cmd[0] == '\0') return 0;

    if (strcmp(cmd, "ls") == 0) {
        cmd_ls(args);
        return 0;
    }
    if (strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
        return 0;
    }
    if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(args);
        return 0;
    }
    if (strcmp(cmd, "touch") == 0) {
        cmd_touch(args);
        return 0;
    }
    if (strcmp(cmd, "write") == 0) {
        cmd_write(args);
        return 0;
    }
    if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd(args);
        return 0;
    }
    if (strcmp(cmd, "tree") == 0) {
        cmd_tree(args);
        return 0;
    }
    if (strcmp(cmd, "stat") == 0) {
        cmd_stat(args);
        return 0;
    }
    if (strcmp(cmd, "initconf") == 0) {
        cmd_initconf(args);
        return 0;
    }

    if (strcmp(cmd, "jobdemo") == 0) {
        run_job_control_demo();
        return 0;
    }

    /* Not a filesystem command - unknown */
    return -1;
}
