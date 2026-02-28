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
#include "include/udp.h"
#include "include/net_device.h"
#include "include/net_rx.h"
#include "include/net.h"
#include "include/dhcp.h"
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

    struct vnode *vp = vfs_resolve_path_ramfs(path);
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

    struct vnode *vp = vfs_resolve_path_ramfs(path);
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
        parent = vfs_resolve_path_ramfs(parent_path);
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

    struct vnode *dir = vfs_mkdir_ramfs(parent, name);
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
    struct vnode *existing = vfs_resolve_path_ramfs(path);
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
        parent = vfs_resolve_path_ramfs(parent_path);
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
    struct vnode *vp = vfs_resolve_path_ramfs(path);
    if (!vp) {
        /* Try to create it */
        cmd_touch(path);
        vp = vfs_resolve_path_ramfs(path);
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

    struct vnode *vp = vfs_resolve_path_ramfs(path);
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

    struct vnode *vp = vfs_resolve_path_ramfs(path);
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

/* ========== rm ========== */

static void cmd_rm(const char *args) {
    const char *path = skip_spaces(args);
    if (*path == '\0') {
        vga_write("rm: missing operand\n");
        return;
    }

    /* Split path into parent directory and basename */
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
        parent = vfs_resolve_path_ramfs(parent_path);
        name = last_slash + 1;
    }

    if (!parent || *name == '\0') {
        vga_write("rm: cannot remove '");
        vga_write(path);
        vga_write("': invalid path\n");
        return;
    }

    if (vfs_unlink(parent, name) != 0) {
        vga_write("rm: cannot remove '");
        vga_write(path);
        vga_write("': No such file or directory\n");
    }
}

/* ========== mv ========== */

static void cmd_mv(const char *args) {
    char src[256];
    const char *rest = next_token(args, src, 256);
    char dst[256];
    next_token(rest, dst, 256);

    if (src[0] == '\0' || dst[0] == '\0') {
        vga_write("mv: usage: mv <src> <dst>\n");
        return;
    }

    /* Helper: split path into parent path + basename */
    const char *src_slash = src;
    for (const char *p = src; *p; p++) if (*p == '/') src_slash = p;
    struct vnode *src_parent;
    const char *src_name;
    char src_parent_path[256];
    if (src_slash == src) {
        src_parent = vfs_get_root();
        src_name = src + (src[0] == '/' ? 1 : 0);
    } else {
        int len = (int)(src_slash - src);
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) src_parent_path[i] = src[i];
        src_parent_path[len] = '\0';
        src_parent = vfs_resolve_path_ramfs(src_parent_path);
        src_name = src_slash + 1;
    }

    const char *dst_slash = dst;
    for (const char *p = dst; *p; p++) if (*p == '/') dst_slash = p;
    struct vnode *dst_parent;
    const char *dst_name;
    char dst_parent_path[256];
    if (dst_slash == dst) {
        dst_parent = vfs_get_root();
        dst_name = dst + (dst[0] == '/' ? 1 : 0);
    } else {
        int len = (int)(dst_slash - dst);
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) dst_parent_path[i] = dst[i];
        dst_parent_path[len] = '\0';
        dst_parent = vfs_resolve_path_ramfs(dst_parent_path);
        dst_name = dst_slash + 1;
    }

    if (!src_parent || !dst_parent) {
        vga_write("mv: cannot stat: No such file or directory\n");
        return;
    }

    /* If dst is an existing directory, move src inside it */
    struct vnode *dst_vp = vfs_resolve_path_ramfs(dst);
    if (dst_vp && dst_vp->v_type == VDIR) {
        dst_parent = dst_vp;
        dst_name = src_name;  /* keep the basename */
    }

    if (vfs_rename(src_parent, src_name, dst_parent, dst_name) != 0) {
        vga_write("mv: cannot move '");
        vga_write(src);
        vga_write("' to '");
        vga_write(dst);
        vga_write("'\n");
    }
}

/* ========== chmod ========== */

static void cmd_chmod(const char *args) {
    char mode_str[16];
    const char *rest = next_token(args, mode_str, 16);
    char path[256];
    next_token(rest, path, 256);

    if (mode_str[0] == '\0' || path[0] == '\0') {
        vga_write("chmod: usage: chmod <octal-mode> <path>\n");
        return;
    }

    /* Parse octal mode string */
    uint16_t mode = 0;
    for (int i = 0; mode_str[i]; i++) {
        char c = mode_str[i];
        if (c < '0' || c > '7') {
            vga_write("chmod: invalid mode '");
            vga_write(mode_str);
            vga_write("'\n");
            return;
        }
        mode = (uint16_t)((mode << 3) | (uint16_t)(c - '0'));
    }

    struct vnode *vp = vfs_resolve_path_ramfs(path);
    if (!vp) {
        vga_write("chmod: cannot access '");
        vga_write(path);
        vga_write("': No such file or directory\n");
        return;
    }

    if (vfs_chmod(vp, mode) != 0) {
        vga_write("chmod: failed to change mode of '");
        vga_write(path);
        vga_write("'\n");
    }
}

/* ========== netinfo ========== */

static void vga_print_ip(ipv4_addr_t ip)
{
    char buf[4];
    for (int i = 0; i < 4; i++) {
        int v = ip.bytes[i], n = 0;
        if (v == 0) { buf[n++] = '0'; }
        else { while (v > 0) { buf[n++] = (char)('0' + v % 10); v /= 10; } }
        /* reverse */
        for (int a = 0, b = n-1; a < b; a++, b--) { char c = buf[a]; buf[a] = buf[b]; buf[b] = c; }
        buf[n] = '\0';
        vga_write(buf);
        if (i < 3) vga_write(".");
    }
}

static void vga_print_mac(mac_addr_t mac)
{
    const char *hex = "0123456789abcdef";
    char buf[3] = {0, 0, 0};
    for (int i = 0; i < 6; i++) {
        buf[0] = hex[mac.bytes[i] >> 4];
        buf[1] = hex[mac.bytes[i] & 0xF];
        buf[2] = '\0';
        vga_write(buf);
        if (i < 5) vga_write(":");
    }
}

static void cmd_netinfo(const char *args)
{
    (void)args;
    int count = net_device_count();
    if (count == 0) { vga_write("No network devices registered.\n"); return; }
    for (int i = 0; i < count; i++) {
        net_device_t *dev = net_device_get(i);
        if (!dev) continue;
        vga_write(dev->name);
        vga_write(": ");
        vga_write(dev->link_up ? "UP" : "DOWN");
        vga_write("\n  mac     ");
        vga_print_mac(dev->mac);
        vga_write("\n  ip      ");
        vga_print_ip(dev->ip);
        vga_write("\n  netmask ");
        vga_print_ip(dev->netmask);
        vga_write("\n  gateway ");
        vga_print_ip(dev->gateway);
        vga_write("\n");
    }
}

/* ========== dhcp ========== */

static void cmd_dhcp(const char *args)
{
    (void)args;
    net_device_t *dev = net_device_get_by_name("eth0");
    if (!dev) { vga_write("dhcp: eth0 not found\n"); return; }
    if (!dev->link_up) { vga_write("dhcp: eth0 is down\n"); return; }
    vga_write("dhcp: running DHCP on eth0...\n");
    int rc = dhcp_discover(dev);
    if (rc == 0) {
        vga_write("dhcp: configured eth0: ");
        vga_print_ip(dev->ip);
        vga_write(" gw ");
        vga_print_ip(dev->gateway);
        vga_write("\n");
    } else {
        vga_write("dhcp: failed (timeout)\n");
    }
}

/* ========== udpsend ========== */

/**
 * @brief Send a single UDP datagram over the default NIC.
 *
 * Usage: udpsend <ip> <port> <message>
 * Example: udpsend 10.0.2.2 9 hello
 */
static void cmd_udpsend(const char *args)
{
    if (!args || *args == '\0') {
        vga_write("usage: udpsend <ip> <port> <message>\n");
        return;
    }

    /* Parse destination IP: a.b.c.d */
    const char *p = skip_spaces(args);
    uint8_t ip[4] = {0};
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; }
        ip[i] = (uint8_t)v;
        if (i < 3) { if (*p == '.') p++; else { vga_write("udpsend: bad IP\n"); return; } }
    }
    if (*p != ' ' && *p != '\t') { vga_write("udpsend: bad IP\n"); return; }
    while (*p == ' ' || *p == '\t') p++;

    /* Parse destination port */
    uint32_t port = 0;
    while (*p >= '0' && *p <= '9') { port = port * 10 + (uint32_t)(*p - '0'); p++; }
    if (port == 0 || port > 65535) { vga_write("udpsend: bad port\n"); return; }
    while (*p == ' ' || *p == '\t') p++;

    /* Remainder is the message */
    const char *msg = p;
    uint16_t msg_len = 0;
    while (msg[msg_len]) msg_len++;
    if (msg_len == 0) { vga_write("udpsend: empty message\n"); return; }

    /* Get the default NIC (eth0 if available, else lo) */
    net_device_t *dev = net_device_get_by_name("eth0");
    if (!dev) dev = net_device_get_by_name("lo");
    if (!dev) { vga_write("udpsend: no NIC available\n"); return; }

    ipv4_addr_t dst = {{ip[0], ip[1], ip[2], ip[3]}};

    /* udp_send may fail on the first try because ipv4_send fires an ARP
     * request and immediately returns -1 (async ARP).  Poll the NIC to
     * process the ARP reply, then retry up to 5 times. */
    int rc = -1;
    for (int attempt = 0; attempt < 5 && rc != 0; attempt++) {
        rc = udp_send(dev, dst,
                      49152u,          /* ephemeral source port */
                      (uint16_t)port,
                      (const void *)msg, msg_len);
        if (rc != 0) {
            /* Pump the RX ring so the ARP reply can arrive */
            for (int i = 0; i < 50000; i++) {
                net_device_poll_all();
                net_rx_process();
            }
        }
    }

    if (rc == 0) {
        vga_write("udpsend: sent ");
        /* simple decimal print of msg_len */
        char nbuf[8];
        int ni = 0;
        uint16_t tmp = msg_len;
        if (tmp == 0) { nbuf[ni++] = '0'; }
        else { while (tmp > 0) { nbuf[ni++] = (char)('0' + tmp % 10); tmp /= 10; } }
        /* reverse */
        for (int a = 0, b = ni-1; a < b; a++, b--) { char c = nbuf[a]; nbuf[a] = nbuf[b]; nbuf[b] = c; }
        nbuf[ni] = '\0';
        vga_write(nbuf);
        vga_write(" bytes\n");
    } else {
        vga_write("udpsend: send failed\n");
    }
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
    if (strcmp(cmd, "rm") == 0) {
        cmd_rm(args);
        return 0;
    }
    if (strcmp(cmd, "mv") == 0) {
        cmd_mv(args);
        return 0;
    }
    if (strcmp(cmd, "chmod") == 0) {
        cmd_chmod(args);
        return 0;
    }
    if (strcmp(cmd, "udpsend") == 0) {
        cmd_udpsend(args);
        return 0;
    }
    if (strcmp(cmd, "netinfo") == 0) {
        cmd_netinfo(args);
        return 0;
    }
    if (strcmp(cmd, "dhcp") == 0) {
        cmd_dhcp(args);
        return 0;
    }

    /* Not a filesystem command - unknown */
    return -1;
}
