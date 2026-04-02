/**
 * @file tty.h
 * @brief TTY & Terminal Subsystem
 *
 * Provides a proper terminal abstraction between raw hardware drivers
 * (VGA framebuffer, PS/2 keyboard) and userspace/kernel consumers.
 *
 * Architecture:
 *   Keyboard HW ──► TTY (line discipline) ──► process / syscall
 *   process / syscall ──► TTY (output) ──► VGA framebuffer
 *
 * The TTY owns:
 *  - Line buffer with insert-mode editing & cursor tracking
 *  - Command history ring
 *  - Visible cursor rendering (underline)
 *  - Scancode-to-ASCII translation for input polling
 *  - Output routing to VGA + optional serial echo
 */

#ifndef TTY_H
#define TTY_H

#include <stdint.h>
#include <stddef.h>

/* ====== Configuration constants ====== */

#define TTY_LINE_MAX     128   /* max editable line length (including NUL) */
#define TTY_HISTORY_SIZE   8   /* number of history slots (ring buffer)    */

/* ====== TTY structure ====== */

/**
 * @brief Represents a single terminal device.
 *
 * In a multi-terminal system you'd allocate several of these;
 * for now there is one global console TTY (tty0).
 */
typedef struct tty {
    /* --- Line buffer & editing state --- */
    char  line_buf[TTY_LINE_MAX];  /* current editable line               */
    int   line_len;                /* number of characters in line_buf     */
    int   cursor;                  /* insertion point (0 .. line_len)      */

    /* --- Screen anchor (where the editable area starts) --- */
    int   start_row;
    int   start_col;

    /* --- Command history --- */
    char  history[TTY_HISTORY_SIZE][TTY_LINE_MAX];
    int   hist_count;              /* total entries stored so far          */
    int   hist_write;              /* next slot to write (ring index)      */

    /* --- History browsing (transient, only valid during readline) --- */
    int   hist_browse;             /* -1 = editing current line            */
    char  saved_line[TTY_LINE_MAX];/* input saved before browsing history  */
    int   saved_len;

    /* --- Extended scancode state --- */
    int   e0_prefix;               /* set after receiving 0xE0             */

    /* --- Flags --- */
    int   echo;                    /* non-zero: echo input to screen       */
    int   serial_echo;             /* non-zero: also echo to UART          */
    char  echo_mask;               /* if non-zero, display this char instead of real chars (e.g. '*') */
} tty_t;

/* ====== Lifecycle ====== */

/** Initialise the global console TTY (tty0). Call once at boot. */
void tty_init(void);

/** Return a pointer to the console TTY. */
tty_t *tty_console(void);

/* ====== Output ====== */

/** Write a single character through the TTY to VGA (+ optional serial). */
void tty_putc(tty_t *tty, char c);

/** Write a null-terminated string through the TTY. */
void tty_write(tty_t *tty, const char *s);

/** Write exactly `len` bytes through the TTY. */
void tty_write_n(tty_t *tty, const char *s, size_t len);

/** Clear the screen via the TTY. */
void tty_clear(tty_t *tty);

/* ====== Input / Line editing ====== */

/**
 * @brief Read one edited line from the keyboard.
 *
 * Blocks until the user presses Enter.  Handles:
 *  - Left / Right arrow cursor movement
 *  - Up / Down arrow history browsing
 *  - Home / End
 *  - Backspace / Delete
 *  - Insert-mode character entry
 *  - Visible underline cursor
 *
 * @param tty    Terminal to read from.
 * @param buf    Destination buffer (caller-provided, may be userspace).
 * @param maxlen Max bytes to store including NUL terminator.
 * @return       Number of characters read (excluding NUL), or 0 on empty.
 */
int tty_readline(tty_t *tty, char *buf, int maxlen);

/**
 * @brief Read one edited line with masked echo (e.g. password input).
 *
 * Same as tty_readline but displays mask_char (e.g. '*') for each typed char.
 * Returns -1 on ESC.
 */
int tty_readline_masked(tty_t *tty, char *buf, int maxlen, char mask_char);

/* ====== Cursor rendering helpers (used internally, exposed for tests) ====== */

/** Redraw line buffer chars [from .. len] on screen, clear trailing cell. */
void tty_redraw_line(tty_t *tty, int from);

/** Move the VGA cursor to the screen position matching the buffer cursor. */
void tty_sync_hw_cursor(tty_t *tty);

#endif /* TTY_H */
