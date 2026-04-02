/**
 * @file stdio.c
 * @brief Minimal user-space stdio: putchar, puts, printf.
 *
 * All output goes to STDOUT_FILENO (fd 1) via sys_write.
 */

#include "stdio.h"
#include "syscall.h"
#include <string.h>

int putchar(int c) {
    char ch = (char)c;
    sys_write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    size_t len = strlen(s);
    sys_write(STDOUT_FILENO, s, len);
    sys_write(STDOUT_FILENO, "\n", 1);
    return 0;
}

/* Simple printf — supports %d, %u, %x, %s, %c, %p, %% */
static void print_unsigned(char *buf, int *pos, uint64_t val, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[20];
    int i = 0;
    if (val == 0) { tmp[i++] = '0'; }
    else while (val) { tmp[i++] = digits[val % base]; val /= base; }
    while (--i >= 0) buf[(*pos)++] = tmp[i];
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char buf[512];
    int pos = 0;

    for (; *fmt && pos < 500; fmt++) {
        if (*fmt != '%') { buf[pos++] = *fmt; continue; }
        fmt++;
        switch (*fmt) {
        case 'd': {
            int64_t v = va_arg(ap, int64_t);
            if (v < 0) { buf[pos++] = '-'; v = -v; }
            print_unsigned(buf, &pos, (uint64_t)v, 10);
            break;
        }
        case 'u':
            print_unsigned(buf, &pos, va_arg(ap, uint64_t), 10);
            break;
        case 'x':
            print_unsigned(buf, &pos, va_arg(ap, uint64_t), 16);
            break;
        case 'p':
            buf[pos++] = '0'; buf[pos++] = 'x';
            print_unsigned(buf, &pos, va_arg(ap, uint64_t), 16);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < 500) buf[pos++] = *s++;
            break;
        }
        case 'c':
            buf[pos++] = (char)va_arg(ap, int);
            break;
        case '%':
            buf[pos++] = '%';
            break;
        default:
            buf[pos++] = '%';
            buf[pos++] = *fmt;
            break;
        }
    }

    va_end(ap);
    sys_write(STDOUT_FILENO, buf, (size_t)pos);
    return pos;
}
