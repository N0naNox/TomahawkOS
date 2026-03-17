#include <stdint.h>


#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_SFMASK      0xC0000084
#define MSR_KERNEL_GS_BASE 0xC0000102

extern void syscall_entry(); // פונקציית האסמבלי שתקבל את הקריאה


static struct {
    uint64_t kernel_stack;       // Offset 0x00
    uint64_t user_stack_scratch; // Offset 0x08
    uint64_t junk;               // Offset 0x10 (המקום שבו האסמבלי שומר את RSP)
} __attribute__((packed)) cpu_info;


void syscall_init() {
    // 1. הפעלת ה-Syscall Extensions ב-EFER
    uint32_t efer_low, efer_high;
    __asm__ volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(MSR_EFER));
    efer_low |= 1; // System Call Extensions enable
    __asm__ volatile("wrmsr" : : "a"(efer_low), "d"(efer_high), "c"(MSR_EFER));

    // 2. הגדרת הסגמנטים (STAR)
    // הביטים 32-47 הם ה-Kernel CS/SS  
    // הביטים 48-63 הם ה-User base selector
    // SYSRET: CS = (STAR[48:63] + 16) | 3, SS = (STAR[48:63] + 8) | 3
    // For User CS=0x20|3=0x23, User SS=0x18|3=0x1B:
    // Need STAR[48:63] = 0x10 (so +16 = 0x20, +8 = 0x18)
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    uint32_t low = (uint32_t)star;
    uint32_t high = (uint32_t)(star >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(MSR_STAR));

    // 3. רישום כתובת ה-Handler (LSTAR)
    uint64_t lstar = (uint64_t)syscall_entry;
    __asm__ volatile("wrmsr" : : "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)), "c"(MSR_LSTAR));

    // 4. מסכת רגיסטרים (SFMASK) - איזה ביטים ב-RFLAGS יכבו בכניסה ל-Syscall (בד"כ פסיקות)
    __asm__ volatile("wrmsr" : : "a"(0x200), "d"(0), "c"(MSR_SFMASK)); 


    static uint8_t sys_stack[16384];   /* 16 KB – 8 KB was too tight for VFS/shell syscalls */
    cpu_info.kernel_stack = (uintptr_t)sys_stack + sizeof(sys_stack);

    uint64_t gs_base = (uintptr_t)&cpu_info;
    
    // כתיבה ל-MSR_KERNEL_GS_BASE
    low = (uint32_t)gs_base;
    high = (uint32_t)(gs_base >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(MSR_KERNEL_GS_BASE));

}