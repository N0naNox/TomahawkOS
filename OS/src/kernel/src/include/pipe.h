#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

struct pcb;  /* forward declaration */

/**
 * Create a pipe and install the read/write ends into proc's fd table.
 * On success fds[0] = read end, fds[1] = write end.
 * Returns 0 on success, negative errno on failure.
 */
int pipe_create(struct pcb *proc, int fds[2]);

#endif
