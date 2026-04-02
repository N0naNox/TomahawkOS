/**
 * @file pipe.c
 * @brief Kernel pipe implementation — unidirectional byte stream.
 *
 * A pipe is a 4 KiB ring buffer shared between a read-end vnode and a
 * write-end vnode.  Reads block (busy-wait + yield) when empty; writes
 * block when full.  Closing the write end causes reads to return 0 (EOF).
 */

#include "include/pipe.h"
#include "include/fd.h"
#include "include/proc.h"
#include "include/errno.h"
#include "include/vnode.h"
#include "include/file.h"
#include "scheduler.h"
#include <string.h>

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES       32

struct pipe {
    char     buf[PIPE_BUF_SIZE];
    uint32_t rd;            /* read index   */
    uint32_t wr;            /* write index  */
    int      readers;       /* open read-end count  */
    int      writers;       /* open write-end count */
    int      in_use;
};

static struct pipe pipe_pool[MAX_PIPES];

static struct pipe *pipe_alloc_ring(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipe_pool[i].in_use) {
            memset(&pipe_pool[i], 0, sizeof(struct pipe));
            pipe_pool[i].in_use = 1;
            return &pipe_pool[i];
        }
    }
    return NULL;
}

/* ----- vnode ops for the read end ----- */

static int pipe_vop_read(struct vnode *vp, void *buf, size_t nbyte) {
    struct pipe *p = (struct pipe *)vp->v_data;
    if (!p) return -1;

    /* Block while empty and writers still exist */
    while (p->rd == p->wr && p->writers > 0)
        scheduler_yield();

    if (p->rd == p->wr)
        return 0;  /* EOF — no writers left */

    size_t avail = (p->wr - p->rd + PIPE_BUF_SIZE) % PIPE_BUF_SIZE;
    if (nbyte > avail) nbyte = avail;

    size_t copied = 0;
    char *dst = (char *)buf;
    while (copied < nbyte) {
        dst[copied++] = p->buf[p->rd % PIPE_BUF_SIZE];
        p->rd = (p->rd + 1) % PIPE_BUF_SIZE;
    }
    return (int)copied;
}

static int pipe_vop_close_read(struct vnode *vp) {
    struct pipe *p = (struct pipe *)vp->v_data;
    if (p) p->readers--;
    return 0;
}

/* ----- vnode ops for the write end ----- */

static int pipe_vop_write(struct vnode *vp, void *buf, size_t nbyte) {
    struct pipe *p = (struct pipe *)vp->v_data;
    if (!p) return -1;

    if (p->readers <= 0)
        return -1;  /* broken pipe */

    size_t written = 0;
    const char *src = (const char *)buf;
    while (written < nbyte) {
        uint32_t next = (p->wr + 1) % PIPE_BUF_SIZE;
        /* Block while full */
        while (next == p->rd && p->readers > 0)
            scheduler_yield();
        if (p->readers <= 0)
            break;  /* broken pipe */
        p->buf[p->wr] = src[written++];
        p->wr = next;
    }
    return (int)written;
}

static int pipe_vop_close_write(struct vnode *vp) {
    struct pipe *p = (struct pipe *)vp->v_data;
    if (p) p->writers--;
    return 0;
}

/* ----- static vnode / ops pools ----- */

static struct vnode_ops pipe_read_ops = {
    .vop_read  = pipe_vop_read,
    .vop_close = pipe_vop_close_read,
};

static struct vnode_ops pipe_write_ops = {
    .vop_write = pipe_vop_write,
    .vop_close = pipe_vop_close_write,
};

#define PIPE_VNODE_POOL 64
static struct vnode pipe_vnodes[PIPE_VNODE_POOL];
static int pipe_vnode_next = 0;

static struct vnode *pipe_vnode_alloc(void) {
    if (pipe_vnode_next >= PIPE_VNODE_POOL) return NULL;
    struct vnode *vp = &pipe_vnodes[pipe_vnode_next++];
    memset(vp, 0, sizeof(*vp));
    vp->v_type = VCHR;
    return vp;
}

/* ----- public API ----- */

int pipe_create(struct pcb *proc, int fds[2]) {
    if (!proc || !fds) return -EINVAL;

    struct pipe *p = pipe_alloc_ring();
    if (!p) return -ENOMEM;

    struct vnode *rd_vp = pipe_vnode_alloc();
    struct vnode *wr_vp = pipe_vnode_alloc();
    if (!rd_vp || !wr_vp) {
        p->in_use = 0;
        return -ENOMEM;
    }

    rd_vp->v_op   = &pipe_read_ops;
    rd_vp->v_data = p;
    wr_vp->v_op   = &pipe_write_ops;
    wr_vp->v_data = p;

    struct file *rf = file_alloc(rd_vp, O_RDONLY);
    struct file *wf = file_alloc(wr_vp, O_WRONLY);
    if (!rf || !wf) {
        if (rf) rf->refcount = 0;
        if (wf) wf->refcount = 0;
        p->in_use = 0;
        return -ENOMEM;
    }

    int rfd = fd_alloc(proc, rf);
    if (rfd < 0) {
        rf->refcount = 0;
        wf->refcount = 0;
        p->in_use = 0;
        return rfd;
    }
    rf->refcount--;  /* fd_alloc bumped to 2, drop initial 1 */

    int wfd = fd_alloc(proc, wf);
    if (wfd < 0) {
        fd_close(proc, rfd);
        wf->refcount = 0;
        p->in_use = 0;
        return wfd;
    }
    wf->refcount--;

    p->readers = 1;
    p->writers = 1;

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}
