#include "syscall_init.h"

#define IA32_EFER  0xC0000080
#define IA32_LSTAR 0xC0000082
#define IA32_STAR  0xC0000081
#define IA32_FMASK 0xC0000084

extern void syscall_entry(void);

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

void syscall_init(void)
{
    /* Enable syscall/sysret */
    wrmsr(IA32_EFER, 1);

    /* Set syscall entry RIP */
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry);

    /* Kernel CS = 0x08, User CS = 0x1B */
    wrmsr(IA32_STAR, ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32));

    /* Mask IF during syscall */
    wrmsr(IA32_FMASK, 0x200);
}
