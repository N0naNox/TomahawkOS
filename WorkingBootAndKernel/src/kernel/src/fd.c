/**
 * @file fd.c
 * @brief File-descriptor table management.
 *
 * Static pool of struct file objects and per-process fd_table helpers.
 */

#include "include/fd.h"
#include "include/proc.h"
#include "include/errno.h"
#include "include/vfs.h"
#include "include/tty.h"
#include <uart.h>
#include <string.h>

/* ---------- static file pool (no kfree needed) ---------- */
#define FILE_POOL_SIZE 128
static struct file file_pool[FILE_POOL_SIZE];
static int file_pool_inited = 0;

static void file_pool_init(void) {
    if (file_pool_inited) return;
    memset(file_pool, 0, sizeof(file_pool));
    file_pool_inited = 1;
}

struct file *file_alloc(struct vnode *vp, uint16_t flags) {
    file_pool_init();
    for (int i = 0; i < FILE_POOL_SIZE; i++) {
        if (file_pool[i].refcount == 0) {
            file_pool[i].f_vnode  = vp;
            file_pool[i].f_offset = 0;
            file_pool[i].f_flags  = flags;
            file_pool[i].refcount = 1;
            return &file_pool[i];
        }
    }
    return NULL;  /* pool exhausted */
}

static void file_free(struct file *f) {
    if (!f) return;
    f->refcount--;
    if (f->refcount <= 0) {
        if (f->f_vnode && f->f_vnode->v_op && f->f_vnode->v_op->vop_close)
            f->f_vnode->v_op->vop_close(f->f_vnode);
        f->f_vnode  = NULL;
        f->f_offset = 0;
        f->f_flags  = 0;
        f->refcount = 0;
    }
}

/* ---------- per-process fd helpers ---------- */

int fd_alloc(struct pcb *proc, struct file *f) {
    if (!proc || !f) return -EINVAL;
    for (int i = 0; i < MAX_FILES; i++) {
        if (proc->fd_table[i] == NULL) {
            proc->fd_table[i] = f;
            f->refcount++;
            return i;
        }
    }
    return -EMFILE;
}

int fd_close(struct pcb *proc, int fd) {
    if (!proc || fd < 0 || fd >= MAX_FILES) return -EBADF;
    struct file *f = proc->fd_table[fd];
    if (!f) return -EBADF;
    proc->fd_table[fd] = NULL;
    file_free(f);
    return 0;
}

struct file *fd_get(struct pcb *proc, int fd) {
    if (!proc || fd < 0 || fd >= MAX_FILES) return NULL;
    return proc->fd_table[fd];
}

int fd_dup(struct pcb *proc, int src_fd) {
    struct file *f = fd_get(proc, src_fd);
    if (!f) return -EBADF;
    return fd_alloc(proc, f);
}

void fd_clone(struct pcb *parent, struct pcb *child) {
    if (!parent || !child) return;
    for (int i = 0; i < MAX_FILES; i++) {
        struct file *f = parent->fd_table[i];
        if (f) {
            child->fd_table[i] = f;
            f->refcount++;
        }
    }
}

void fd_close_all(struct pcb *proc) {
    if (!proc) return;
    for (int i = 0; i < MAX_FILES; i++) {
        if (proc->fd_table[i]) {
            fd_close(proc, i);
        }
    }
}

/* ---------- TTY device vnode (console) ---------- */

static int tty_vop_write(struct vnode *vp, void *buf, size_t len) {
    (void)vp;
    tty_write_n(tty_console(), (const char *)buf, len);
    return (int)len;
}

static int tty_vop_read(struct vnode *vp, void *buf, size_t len) {
    (void)vp;
    /* Read one line from console; returns bytes read */
    int n = tty_readline(tty_console(), (char *)buf, (int)len);
    return n;
}

static struct vnode_ops tty_ops = {
    .vop_read  = tty_vop_read,
    .vop_write = tty_vop_write,
};

static struct vnode tty_vnode = {
    .v_type = VCHR,
    .v_op   = &tty_ops,
    .v_data = NULL,
};

/* ---------- stdio init ---------- */

void fd_init_stdio(struct pcb *proc) {
    if (!proc) return;

    /* fd 0 = stdin  (readable) */
    struct file *fin = file_alloc(&tty_vnode, O_RDONLY);
    if (fin) { proc->fd_table[0] = fin; }

    /* fd 1 = stdout (writable) */
    struct file *fout = file_alloc(&tty_vnode, O_WRONLY);
    if (fout) { proc->fd_table[1] = fout; }

    /* fd 2 = stderr (writable) */
    struct file *ferr = file_alloc(&tty_vnode, O_WRONLY);
    if (ferr) { proc->fd_table[2] = ferr; }
}
