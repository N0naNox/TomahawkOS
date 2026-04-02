/**
 * @file tty.c
 * @brief TTY & Terminal Subsystem — implementation
 *
 * Sits between the raw hardware drivers (VGA framebuffer, PS/2 keyboard
 * controller) and the rest of the kernel / userspace.
 *
 * Responsibilities:
 *  1. Output routing  — tty_putc / tty_write  → VGA + optional UART echo
 *  2. Line discipline — tty_readline          → full line editing with
 *     cursor movement, insert-mode, history browsing, Home/End/Delete
 *  3. Cursor rendering — visible underline cursor drawn / erased via VGA helpers
 *  4. Input polling    — direct PS/2 port reads (0x60/0x64) with
 *     scancode set 1 translation + E0 extended-key handling
 */

#include "include/tty.h"
#include "include/vga.h"
#include "include/hal_port_io.h"
#include <uart.h>
#include <stddef.h>

/* ================================================================
 *  Global console TTY (tty0)
 * ================================================================ */

static tty_t tty0;

void tty_init(void) {
    tty_t *t = &tty0;

    /* Line buffer */
    t->line_len    = 0;
    t->cursor      = 0;
    t->start_row   = 0;
    t->start_col   = 0;

    /* History */
    t->hist_count  = 0;
    t->hist_write  = 0;
    t->hist_browse = -1;
    t->saved_len   = 0;

    /* Scancode state */
    t->e0_prefix   = 0;

    /* Flags */
    t->echo        = 1;
    t->serial_echo = 0;
    t->echo_mask   = 0;
}

tty_t *tty_console(void) {
    return &tty0;
}

/* ================================================================
 *  Output path
 * ================================================================ */

void tty_putc(tty_t *tty, char c) {
    vga_putc(c);
    if (tty->serial_echo) {
        uart_putchar(c);
    }
}

void tty_write(tty_t *tty, const char *s) {
    if (!s) return;
    while (*s) {
        tty_putc(tty, *s++);
    }
}

void tty_write_n(tty_t *tty, const char *s, size_t len) {
    if (!s) return;
    for (size_t i = 0; i < len; i++) {
        tty_putc(tty, s[i]);
    }
}

void tty_clear(tty_t *tty) {
    (void)tty;
    vga_clear(0, 7);  /* black bg, light grey fg */
}

/* ================================================================
 *  Cursor / line-display helpers
 * ================================================================ */

void tty_redraw_line(tty_t *tty, int from) {
    for (int i = from; i <= tty->line_len; i++) {
        int screen_pos = tty->start_col + i;
        int row = tty->start_row + screen_pos / VGA_WIDTH;
        int col = screen_pos % VGA_WIDTH;
        if (row >= VGA_HEIGHT) break;
        if (i < tty->line_len) {
            char dc = (tty->echo_mask) ? tty->echo_mask : tty->line_buf[i];
            vga_draw_char_at(row, col, dc);
        } else {
            vga_clear_char(row, col);  /* erase trailing ghost char */
        }
    }
}

void tty_sync_hw_cursor(tty_t *tty) {
    int screen_pos = tty->start_col + tty->cursor;
    int row = tty->start_row + screen_pos / VGA_WIDTH;
    int col = screen_pos % VGA_WIDTH;
    vga_set_cursor(row, col);
}

/** Clear all visible characters of the current line on screen. */
static void tty_clear_visible_line(tty_t *tty) {
    for (int i = 0; i <= tty->line_len; i++) {
        int sp  = tty->start_col + i;
        int row = tty->start_row + sp / VGA_WIDTH;
        int col = sp % VGA_WIDTH;
        if (row < VGA_HEIGHT) {
            vga_clear_char(row, col);
        }
    }
}

/** Copy a history entry into the live line buffer & refresh screen. */
static void tty_load_history_entry(tty_t *tty, int idx, int maxlen) {
    vga_erase_cursor();
    tty_clear_visible_line(tty);

    tty->line_len = 0;
    for (int i = 0; tty->history[idx][i] && tty->line_len < maxlen - 1; i++) {
        tty->line_buf[tty->line_len++] = tty->history[idx][i];
    }
    tty->line_buf[tty->line_len] = '\0';
    tty->cursor = tty->line_len;

    tty_redraw_line(tty, 0);
    tty_sync_hw_cursor(tty);
    vga_draw_cursor();
}

/** Store the current line in the history ring (skip empty lines). */
static void tty_history_push(tty_t *tty) {
    if (tty->line_len == 0) return;

    int slot = tty->hist_write;
    int n = tty->line_len;
    if (n >= TTY_LINE_MAX) n = TTY_LINE_MAX - 1;
    for (int i = 0; i < n; i++) {
        tty->history[slot][i] = tty->line_buf[i];
    }
    tty->history[slot][n] = '\0';

    tty->hist_write = (tty->hist_write + 1) % TTY_HISTORY_SIZE;
    if (tty->hist_count < TTY_HISTORY_SIZE) {
        tty->hist_count++;
    }
}

/* ================================================================
 *  Scancode set 1 → ASCII map  (only make-codes, lowercase)
 * ================================================================ */

static const char sc_to_ascii[128] = {
    /*0x00*/ 0,   27,  '1', '2', '3', '4', '5', '6',
    /*0x08*/ '7', '8', '9', '0', '-', '=', '\b','\t',
    /*0x10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /*0x18*/ 'o', 'p', '[', ']', '\n', 0,  'a', 's',
    /*0x20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    /*0x28*/'\'', '`',  0, '\\', 'z', 'x', 'c', 'v',
    /*0x30*/ 'b', 'n', 'm', ',', '.', '/',  0,  '*',
    /*0x38*/  0,  ' ',  0,   0,   0,   0,   0,   0,
    /* 0x40 – 0x7F all 0 */
};

/* Shifted scancode table */
static const char sc_to_ascii_shift[128] = {
    /*0x00*/ 0,   27,  '!', '@', '#', '$', '%', '^',
    /*0x08*/ '&', '*', '(', ')', '_', '+', '\b','\t',
    /*0x10*/ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    /*0x18*/ 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
    /*0x20*/ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    /*0x28*/ '"', '~',  0, '|', 'Z', 'X', 'C', 'V',
    /*0x30*/ 'B', 'N', 'M', '<', '>', '?',  0,  '*',
    /*0x38*/  0,  ' ',  0,   0,   0,   0,   0,   0,
};

/* ================================================================
 *  tty_readline — the heart of the line discipline
 * ================================================================ */

int tty_readline(tty_t *tty, char *buf, int maxlen) {
    if (!buf || maxlen <= 1) return 0;
    if (maxlen > TTY_LINE_MAX) maxlen = TTY_LINE_MAX;

    /* Reset editing state */
    tty->line_len    = 0;
    tty->cursor      = 0;
    tty->e0_prefix   = 0;
    tty->hist_browse = -1;
    tty->saved_len   = 0;

    /* Modifier state (local to this call) */
    int shift_held = 0;
    int caps_lock  = 0;

    /* Record where the editable area starts on screen */
    vga_get_cursor(&tty->start_row, &tty->start_col);

    /* Show the initial cursor */
    vga_draw_cursor();

    for (;;) {
        /* ---- Poll PS/2 keyboard controller ---- */
        if (!(hal_inb(0x64) & 0x01)) {
            __asm__ volatile("pause");
            continue;
        }
        uint8_t sc = hal_inb(0x60);

        /* E0 prefix → next byte is an extended key */
        if (sc == 0xE0) {
            tty->e0_prefix = 1;
            continue;
        }

        /* ---- Modifier tracking ---- */
        /* Shift press */
        if (sc == 0x2A || sc == 0x36) { shift_held = 1; continue; }
        /* Shift release */
        if (sc == 0xAA || sc == 0xB6) { shift_held = 0; continue; }
        /* CapsLock press — toggle */
        if (sc == 0x3A) { caps_lock = !caps_lock; continue; }

        /* Ignore break (key-release) codes */
        if (sc & 0x80) {
            tty->e0_prefix = 0;
            continue;
        }

        /* ========== Extended keys (arrows, Home, End, Delete) ========== */
        if (tty->e0_prefix) {
            tty->e0_prefix = 0;

            switch (sc) {

            /* ---- Left arrow ---- */
            case 0x4B:
                if (tty->cursor > 0) {
                    vga_erase_cursor();
                    tty->cursor--;
                    tty_sync_hw_cursor(tty);
                    vga_draw_cursor();
                }
                break;

            /* ---- Right arrow ---- */
            case 0x4D:
                if (tty->cursor < tty->line_len) {
                    vga_erase_cursor();
                    tty->cursor++;
                    tty_sync_hw_cursor(tty);
                    vga_draw_cursor();
                }
                break;

            /* ---- Up arrow — older history ---- */
            case 0x48:
            {
                if (tty->hist_count == 0) break;
                if (tty->hist_browse == -1) {
                    /* Save current input before browsing */
                    for (int i = 0; i < tty->line_len; i++)
                        tty->saved_line[i] = tty->line_buf[i];
                    tty->saved_line[tty->line_len] = '\0';
                    tty->saved_len = tty->line_len;
                    tty->hist_browse = 0;
                } else if (tty->hist_browse < tty->hist_count - 1) {
                    tty->hist_browse++;
                } else {
                    break; /* already at oldest */
                }
                int idx = (tty->hist_write - 1 - tty->hist_browse
                           + TTY_HISTORY_SIZE) % TTY_HISTORY_SIZE;
                tty_load_history_entry(tty, idx, maxlen);
                break;
            }

            /* ---- Down arrow — newer history ---- */
            case 0x50:
            {
                if (tty->hist_browse < 0) break;

                vga_erase_cursor();
                tty_clear_visible_line(tty);

                tty->hist_browse--;
                if (tty->hist_browse < 0) {
                    /* Restore the saved current input */
                    tty->line_len = tty->saved_len;
                    for (int i = 0; i < tty->line_len; i++)
                        tty->line_buf[i] = tty->saved_line[i];
                    tty->line_buf[tty->line_len] = '\0';
                } else {
                    int idx = (tty->hist_write - 1 - tty->hist_browse
                               + TTY_HISTORY_SIZE) % TTY_HISTORY_SIZE;
                    /* Re-use load helper without double-clear */
                    tty->line_len = 0;
                    for (int i = 0; tty->history[idx][i]
                                    && tty->line_len < maxlen - 1; i++) {
                        tty->line_buf[tty->line_len++] = tty->history[idx][i];
                    }
                    tty->line_buf[tty->line_len] = '\0';
                }
                tty->cursor = tty->line_len;
                tty_redraw_line(tty, 0);
                tty_sync_hw_cursor(tty);
                vga_draw_cursor();
                break;
            }

            /* ---- Home ---- */
            case 0x47:
                if (tty->cursor != 0) {
                    vga_erase_cursor();
                    tty->cursor = 0;
                    tty_sync_hw_cursor(tty);
                    vga_draw_cursor();
                }
                break;

            /* ---- End ---- */
            case 0x4F:
                if (tty->cursor != tty->line_len) {
                    vga_erase_cursor();
                    tty->cursor = tty->line_len;
                    tty_sync_hw_cursor(tty);
                    vga_draw_cursor();
                }
                break;

            /* ---- Delete ---- */
            case 0x53:
                if (tty->cursor < tty->line_len) {
                    vga_erase_cursor();
                    for (int i = tty->cursor; i < tty->line_len - 1; i++)
                        tty->line_buf[i] = tty->line_buf[i + 1];
                    tty->line_len--;
                    tty->line_buf[tty->line_len] = '\0';
                    tty_redraw_line(tty, tty->cursor);
                    tty_sync_hw_cursor(tty);
                    vga_draw_cursor();
                }
                break;

            } /* switch (extended sc) */
            continue;
        }

        /* ========== Normal (non-extended) keys ========== */
        char ch = 0;
        if (sc < sizeof(sc_to_ascii)) {
            /* Determine if shifted: for alpha keys, CapsLock inverts shift.
               For everything else, only Shift matters. */
            int use_shift = shift_held;
            char lower = sc_to_ascii[sc];
            if (caps_lock && lower >= 'a' && lower <= 'z')
                use_shift = !use_shift;
            ch = use_shift ? sc_to_ascii_shift[sc] : sc_to_ascii[sc];
        }
        if (!ch) continue;

        /* ---- ESC — cancel input ---- */
        if (ch == 27) {
            vga_erase_cursor();
            vga_putc('\n');
            buf[0] = '\0';
            return -1;
        }

        /* ---- Enter / Return ---- */
        if (ch == '\n' || ch == '\r') {
            vga_erase_cursor();
            tty->line_buf[tty->line_len] = '\0';

            /* Move VGA cursor past the text, then newline */
            tty->cursor = tty->line_len;
            tty_sync_hw_cursor(tty);
            vga_putc('\n');

            /* Push into history ring */
            tty_history_push(tty);

            /* Copy result to caller buffer */
            int n = tty->line_len;
            if (n >= maxlen) n = maxlen - 1;
            for (int i = 0; i < n; i++) buf[i] = tty->line_buf[i];
            buf[n] = '\0';

            return n;
        }

        /* ---- Backspace ---- */
        if (ch == '\b') {
            if (tty->cursor > 0) {
                vga_erase_cursor();
                for (int i = tty->cursor - 1; i < tty->line_len - 1; i++)
                    tty->line_buf[i] = tty->line_buf[i + 1];
                tty->line_len--;
                tty->cursor--;
                tty->line_buf[tty->line_len] = '\0';
                tty_redraw_line(tty, tty->cursor);
                tty_sync_hw_cursor(tty);
                vga_draw_cursor();
            }
            continue;
        }

        /* ---- Tab — ignore for now ---- */
        if (ch == '\t') continue;

        /* ---- Printable character — insert at cursor ---- */
        if (ch >= 32 && ch < 127 && tty->line_len < maxlen - 1) {
            vga_erase_cursor();

            /* Shift characters at cursor rightward */
            for (int i = tty->line_len; i > tty->cursor; i--)
                tty->line_buf[i] = tty->line_buf[i - 1];

            tty->line_buf[tty->cursor] = ch;
            tty->line_len++;
            tty->cursor++;
            tty->line_buf[tty->line_len] = '\0';

            tty_redraw_line(tty, tty->cursor - 1);
            tty_sync_hw_cursor(tty);
            vga_draw_cursor();
        }
    }
    /* not reached */
}

int tty_readline_masked(tty_t *tty, char *buf, int maxlen, char mask_char) {
    tty->echo_mask = mask_char;
    int r = tty_readline(tty, buf, maxlen);
    tty->echo_mask = 0;
    return r;
}
