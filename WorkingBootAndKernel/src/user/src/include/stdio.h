#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Standard file descriptor numbers */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);

#endif
