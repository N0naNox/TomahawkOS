#ifndef FD_H
#define FD_H

#include <stdint.h>
#include "file.h"

struct pcb;  /* forward declaration */

/* Allocate the lowest available fd in proc's table, pointing to f.
 * Increments f->refcount.  Returns fd >= 0 or -EMFILE. */
int fd_alloc(struct pcb *proc, struct file *f);

/* Close fd in proc's table.  Decrements refcount; if it reaches 0,
 * calls vop_close (if defined) and frees the struct file.
 * Returns 0 or -EBADF. */
int fd_close(struct pcb *proc, int fd);

/* Return the struct file* for a given fd, or NULL if invalid. */
struct file *fd_get(struct pcb *proc, int fd);

/* Duplicate src_fd into the lowest available slot.
 * Increments refcount.  Returns new fd >= 0 or negative errno. */
int fd_dup(struct pcb *proc, int src_fd);

/* Clone the entire fd_table from parent into child (for fork).
 * Increments refcount on each open file.  */
void fd_clone(struct pcb *parent, struct pcb *child);

/* Allocate a struct file backed by vnode vp with flags.
 * Sets refcount = 1.  Returns pointer or NULL on ENOMEM. */
struct file *file_alloc(struct vnode *vp, uint16_t flags);

/* Close all open fds for proc (called on exit/reap). */
void fd_close_all(struct pcb *proc);

/* Open fd 0/1/2 on the console TTY for proc. */
void fd_init_stdio(struct pcb *proc);

#endif
