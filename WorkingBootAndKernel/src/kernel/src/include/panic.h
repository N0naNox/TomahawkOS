#pragma once
#include <stdint.h>
#include "idt.h"

/**
 * @brief Halt the system with a full diagnostic dump.
 *
 * Prints the panic reason, all registers, faulting address, error code
 * decode, and a stack trace to both VGA and serial, then halts.
 *
 * @param reason  Human-readable reason string (e.g. "Page Fault")
 * @param regs    Register snapshot from the exception frame (may be NULL)
 * @param cr2     Faulting address for page faults (0 if N/A)
 */
void kernel_panic(const char *reason, const regs_t *regs, uint64_t cr2)
    __attribute__((noreturn));

/**
 * @brief Panic with just a message (no register context).
 */
void kernel_panic_msg(const char *reason) __attribute__((noreturn));
