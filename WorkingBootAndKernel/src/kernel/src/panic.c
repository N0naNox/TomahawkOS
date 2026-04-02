/**
 * @file panic.c
 * @brief Kernel panic handler — dumps registers, stack trace, and halts.
 *
 * Outputs to both VGA (user-visible) and serial (debug log).
 */

#include "include/panic.h"
#include "include/vga.h"
#include "include/proc.h"
#include <uart.h>
#include <stdint.h>

/* ---------- local hex/int formatting (no external dependency) ---------- */

static const char hex_chars[] = "0123456789ABCDEF";

static void write_hex(uint64_t val) {
    char buf[19]; /* "0x" + 16 hex + NUL */
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex_chars[(val >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';
    vga_write(buf);
    uart_puts(buf);
}

static void write_str(const char *s) {
    vga_write(s);
    uart_puts(s);
}

static void write_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (val == 0) { buf[--i] = '0'; }
    else { while (val) { buf[--i] = '0' + (val % 10); val /= 10; } }
    write_str(&buf[i]);
}

/* ---------- register dump ---------- */

static void dump_reg(const char *name, uint64_t val) {
    write_str(name);
    write_hex(val);
    write_str("\n");
}

static void dump_registers(const regs_t *r) {
    write_str("--- Registers ---\n");
    dump_reg("  RAX=", r->rax);
    dump_reg("  RBX=", r->rbx);
    dump_reg("  RCX=", r->rcx);
    dump_reg("  RDX=", r->rdx);
    dump_reg("  RSI=", r->rsi);
    dump_reg("  RDI=", r->rdi);
    dump_reg("  RBP=", r->rbp);
    dump_reg("  RSP=", r->rsp);
    dump_reg("  R8 =", r->r8);
    dump_reg("  R9 =", r->r9);
    dump_reg("  R10=", r->r10);
    dump_reg("  R11=", r->r11);
    dump_reg("  R12=", r->r12);
    dump_reg("  R13=", r->r13);
    dump_reg("  R14=", r->r14);
    dump_reg("  R15=", r->r15);
    dump_reg("  RIP=", r->rip);
    dump_reg("  CS =", r->cs);
    dump_reg("  RFLAGS=", r->rflags);
    dump_reg("  SS =", r->ss);
}

/* ---------- error code decode ---------- */

static void decode_error_code(uint64_t err) {
    write_str("--- Error Code Decode ---\n");
    write_str("  Present:      "); write_dec(err & 1);        write_str("\n");
    write_str("  Write:        "); write_dec((err >> 1) & 1); write_str("\n");
    write_str("  User-mode:    "); write_dec((err >> 2) & 1); write_str("\n");
    write_str("  Reserved-bit: "); write_dec((err >> 3) & 1); write_str("\n");
    write_str("  Instr-fetch:  "); write_dec((err >> 4) & 1); write_str("\n");
}

/* ---------- stack trace ---------- */

/* Walk RBP chain (frame pointer based).
 * For each frame: [RBP] points to saved RBP, [RBP+8] is return address. */
static void dump_stack_trace(const regs_t *r) {
    write_str("--- Stack Trace ---\n");

    /* First frame: the faulting RIP itself */
    write_str("  #0  ");
    write_hex(r->rip);

    /* Ring check: if CS lowest 2 bits == 3, fault was in user-mode */
    if ((r->cs & 3) == 3)
        write_str("  [user-mode]\n");
    else
        write_str("  [kernel-mode]\n");

    uint64_t rbp = r->rbp;
    for (int frame = 1; frame < 16; frame++) {
        /* Safety: stop if RBP is NULL, obviously bogus, or not aligned */
        if (rbp == 0 || (rbp & 7) != 0) break;

        /* If user-mode fault, RBP points into user space — read carefully */
        /* For kernel stacks, the pointer is directly accessible.
         * We can't use copy_from_user here (panic context), so we simply
         * check for a sane range and stop if it looks bad. */
        uint64_t *frame_ptr = (uint64_t *)rbp;

        /* Attempt to read — the kernel identity-maps low memory and its
         * own stack, so kernel frames are safe.  User frames may not be
         * mapped; a nested page fault here is harmless (double-fault or
         * we just stop). We'll limit to kernel-space addresses. */
        if ((r->cs & 3) == 0) {
            /* Kernel frames only */
            uint64_t saved_rbp = frame_ptr[0];
            uint64_t ret_addr  = frame_ptr[1];
            if (ret_addr == 0) break;
            write_str("  #");
            write_dec((uint64_t)frame);
            write_str("  ");
            write_hex(ret_addr);
            write_str("\n");
            rbp = saved_rbp;
        } else {
            /* User-mode frame — don't chase pointers across page boundaries */
            write_str("  (user-mode frames not traced)\n");
            break;
        }
    }
}

/* ---------- process info ---------- */

static void dump_process_info(void) {
    pcb_t *p = get_current_process();
    if (!p) {
        write_str("  Process: (none)\n");
        return;
    }
    write_str("  PID=");
    write_dec(p->pid);
    write_str("  UID=");
    write_dec(p->uid);
    write_str("\n");
}

/* ---------- public API ---------- */

void kernel_panic(const char *reason, const regs_t *regs, uint64_t cr2) {
    /* Disable interrupts to prevent further corruption */
    __asm__ volatile("cli");

    write_str("\n========== KERNEL PANIC ==========\n");
    write_str("Reason: ");
    write_str(reason ? reason : "(unknown)");
    write_str("\n");

    if (cr2) {
        write_str("Faulting address: ");
        write_hex(cr2);
        write_str("\n");
    }

    dump_process_info();

    if (regs) {
        write_str("Error code: ");
        write_hex(regs->err_code);
        write_str("\n");
        decode_error_code(regs->err_code);
        dump_registers(regs);
        dump_stack_trace(regs);
    }

    write_str("==================================\n");
    write_str("System halted. Please reboot.\n");

    while (1) { __asm__ volatile("hlt"); }
    __builtin_unreachable();
}

void kernel_panic_msg(const char *reason) {
    kernel_panic(reason, (void*)0, 0);
}
