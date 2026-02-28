#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "include/proc.h"
#include "include/signal.h"
#include "include/sys_proc.h"
#include "include/idt.h"
#include "include/hal_port_io.h"
#include "include/keyboard.h"
#include "include/password_store.h"
#include "include/demos.h"
#include "include/tests.h"
#include "include/shell_fs_cmd.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "uart.h"

/* External saved context from usermode demo */
extern volatile uint64_t usermode_demo_return_rsp;
extern volatile uint64_t usermode_demo_return_rbp;
extern volatile uint64_t usermode_demo_return_rip;
extern volatile int usermode_demo_completed;

/* External saved context from usermode password demo */
extern volatile uint64_t usermode_pass_return_rsp;
extern volatile uint64_t usermode_pass_return_rbp;
extern volatile uint64_t usermode_pass_return_rip;
extern volatile int usermode_pass_completed;

/* Shell state - current logged in user ID (-1 = not logged in) */
static volatile int shell_current_uid = -1;

/* Helper to return to kernel from usermode demo */
static void return_to_kernel_demo(volatile uint64_t* rsp, volatile uint64_t* rbp, 
                                   volatile uint64_t* rip, volatile int* completed) {
    /* Poll keyboard directly for ESC - keep timer masked */
    while (1) {
        if (hal_inb(0x64) & 0x01) {
            uint8_t scancode = hal_inb(0x60);
            if (scancode == 0x01) {  /* ESC scancode */
                break;
            }
        }
        __asm__ volatile("pause");
    }
    
    /* Flush any remaining scancodes */
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60);
    }
    
    /* Restore timer and keyboard IRQs */
    uint8_t pic_mask = hal_inb(0x21);
    pic_mask &= ~0x01;  /* Unmask IRQ0 (timer) */
    pic_mask &= ~0x02;  /* Unmask IRQ1 (keyboard) */
    hal_outb(0x21, pic_mask);
    
    __asm__ volatile("sti");
    
    *completed = 1;
    
    __asm__ volatile("swapgs");
    
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "mov %1, %%rbp\n"
        "jmp *%2\n"
        :
        : "r"(*rsp), "r"(*rbp), "r"(*rip)
    );
}

/* Extended syscall handler with more arguments */
uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, regs_t* regs) 
{
    switch(syscall_num) {
        case SYSCALL_TEST:
            /* Test syscall from usermode demo - we're back in Ring 0! */
            vga_write("\nSyscall received from Ring 3!\n");
            vga_write("Ring 3 -> Ring 0 transition successful!\n");
            vga_write("\n=== User Mode Demo Complete! ===\n");
            vga_write("Press ESC to return to menu.\n");
            
            /* Restore kernel data segments - but NOT GS, it's used for syscall cpu_info */
            __asm__ volatile(
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                ::: "ax"
            );
            
            /* Determine which demo to return to based on which has valid return address */
            if (usermode_pass_return_rip != 0 && !usermode_pass_completed) {
                return_to_kernel_demo(&usermode_pass_return_rsp, &usermode_pass_return_rbp,
                                      &usermode_pass_return_rip, (volatile int*)&usermode_pass_completed);
            } else {
                return_to_kernel_demo(&usermode_demo_return_rsp, &usermode_demo_return_rbp,
                                      &usermode_demo_return_rip, (volatile int*)&usermode_demo_completed);
            }
            
            /* Never reached */
            return 0;

        case SYS_YIELD:
            scheduler_yield();
            return 0;

        case SYS_EXIT:
            return sys_exit();

        case SYS_FORK:
            return sys_fork();

        case SYS_GETPID:
            return sys_getpid();

        case SYS_SIGNAL:
            return sys_signal((int)arg1, (sig_handler_t)arg2);

        case SYS_KILL:
            return sys_kill(arg1, (int)arg2);

        case SYS_SIGRETURN:
            return (uint64_t)signal_return(get_current_process(), regs);

        case SYS_EXEC:
            /* arg1 = path, arg2 = argv */
            return sys_exec((const char*)arg1, (char* const*)arg2);

        case SYS_WAIT:
            /* arg1 = status pointer */
            return sys_wait((int*)arg1);

        case SYS_WAITPID:
            /* arg1 = pid, arg2 = status, arg3 = options */
            return sys_waitpid((int)arg1, (int*)arg2, (int)arg3);

        case SYS_PASS_VERIFY:
            /* arg1 = username, arg2 = password */
            return (uint64_t)password_store_verify((const char*)arg1, (const char*)arg2);

        case SYS_PASS_STORE:
            /* arg1 = username, arg2 = password */
            return (uint64_t)password_store_add((const char*)arg1, (const char*)arg2);

        case SYS_PASS_EXISTS:
            /* arg1 = username */
            return (uint64_t)password_store_user_exists((const char*)arg1);

        case SYS_WRITE:
            /* arg1 = string pointer, arg2 = length */
            /* Debug: print addr and len via UART */
            uart_puts("[SYS_WRITE] str=0x");
            uart_puthex((uint64_t)arg1);
            uart_puts(" len=");
            uart_putu((uint64_t)arg2);
            uart_puts("\n");
            if (arg1 && arg2) {
                const char *str = (const char*)arg1;
                size_t len = (size_t)arg2;
                /* Debug: print first few bytes in hex */
                uart_puts("  first bytes: ");
                for (size_t i = 0; i < 8 && i < len; i++) {
                    uart_puthex((uint8_t)str[i]);
                    uart_puts(" ");
                }
                uart_puts("\n");
                for (size_t i = 0; i < len; i++) {
                    vga_putc(str[i]);
                }
            }
            return 0;

        case SYS_GETCHAR:
            /* Read a character from keyboard using direct polling only */
            /* (keyboard interrupt is masked, so we don't check the buffer) */
            {
                static const char scancode_map[128] = {
                    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
                    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
                    'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
                    'b','n','m',',','.','/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
                };
                char c = 0;
                while (!c) {
                    /* Poll the keyboard controller directly */
                    if (hal_inb(0x64) & 0x01) {
                        uint8_t scancode = hal_inb(0x60);
                        /* Ignore break codes (key release, bit 7 set) */
                        if (!(scancode & 0x80)) {
                            if (scancode < sizeof(scancode_map)) {
                                c = scancode_map[scancode];
                            }
                        }
                    }
                    if (!c) {
                        __asm__ volatile("pause");
                    }
                }
                return (uint64_t)(unsigned char)c;
            }

        case SYS_PUTCHAR:
            /* arg1 = character to print */
            vga_putc((char)arg1);
            return 0;

        case SYS_GETUID: {
            /* Return UID from the current process PCB.
             * Sign-extend so UID -1 (not logged in) is negative in 64-bit. */
            pcb_t *_p = get_current_process();
            return _p ? (uint64_t)(int64_t)(int32_t)_p->uid : (uint64_t)(int64_t)-1;
        }

        case SYS_SETUID: {
            /* Root (UID 0) may set any UID.
             * Guest (UID -1 / not logged in) may set any UID (login).
             * Any user may set UID to -1 (logout).
             * Any user may keep their own UID (no-op). */
            pcb_t *_p = get_current_process();
            if (!_p) return (uint64_t)-1;
            if (_p->uid != 0 && _p->uid != (uint32_t)-1 &&
                (uint32_t)arg1 != (uint32_t)-1 && _p->uid != (uint32_t)arg1) {
                uart_puts("[SHELL] SYS_SETUID: permission denied\n");
                return (uint64_t)-1;  /* EPERM */
            }
            _p->uid = (uint32_t)arg1;
            shell_current_uid = (int)arg1;  /* keep legacy shell var in sync */
            uart_puts("[SHELL] UID set to ");
            uart_putu(_p->uid);
            uart_puts("\n");
            return 0;
        }

        case SYS_GET_USERNAME:
            /* arg1 = buffer, arg2 = buffer size */
            if (shell_current_uid < 0) {
                /* Not logged in - return "guest" */
                const char* guest = "guest";
                char* buf = (char*)arg1;
                int i = 0;
                for (; guest[i] && i < (int)arg2 - 1; i++) {
                    buf[i] = guest[i];
                }
                buf[i] = '\0';
                return 0;
            }
            return (uint64_t)password_store_get_username(shell_current_uid, (char*)arg1, (int)arg2);

        case SYS_CLEAR_SCREEN:
            /* Clear the VGA screen */
            vga_clear(0, 7);  /* black background, light grey text */
            return 0;

        case SYS_SHELL_EXIT:
            /* Exit from shell - return to kernel */
            __asm__ volatile(
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                ::: "ax"
            );
            
            /* Reset shell UID */
            shell_current_uid = -1;
            
            if (usermode_pass_return_rip != 0 && !usermode_pass_completed) {
                /* Restore IRQs */
                uint8_t pic_mask = hal_inb(0x21);
                pic_mask &= ~0x01;  /* Unmask IRQ0 (timer) */
                pic_mask &= ~0x02;  /* Unmask IRQ1 (keyboard) */
                hal_outb(0x21, pic_mask);
                __asm__ volatile("sti");
                
                usermode_pass_completed = 1;
                __asm__ volatile("swapgs");
                __asm__ volatile(
                    "mov %0, %%rsp\n"
                    "mov %1, %%rbp\n"
                    "jmp *%2\n"
                    :
                    : "r"(usermode_pass_return_rsp), "r"(usermode_pass_return_rbp), "r"(usermode_pass_return_rip)
                );
            }
            return 0;

        case SYS_PASS_GET_UID:
            /* arg1 = username, returns UID or -1 if not found */
            return (uint64_t)password_store_get_uid((const char*)arg1);

        case SYS_RUN_VFS_DEMO:
            /* Run VFS demo from shell */
            vga_write("\n");
            run_vfs_demo();
            return 0;

        case SYS_RUN_TESTS:
            /* Run kernel tests from shell */
            vga_write("\n");
            run_kernel_tests();
            return 0;

        case SYS_RUN_FORK_EXEC_WAIT_DEMO:
            /* Run fork-exec-wait demo from shell */
            vga_write("\n");
            run_fork_exec_wait_demo();
            return 0;

        case SYS_SHELL_CMD:
            /* arg1 = pointer to command line string in userspace */
            if (arg1) {
                int ret = shell_fs_dispatch((const char *)arg1);
                return (uint64_t)ret;
            }
            return (uint64_t)-1;

        /* ===== Job control syscalls ===== */
        case SYS_SETPGID:
            return sys_setpgid(arg1, arg2);

        case SYS_GETPGID:
            return sys_getpgid(arg1);

        case SYS_SETSID:
            return sys_setsid();

        case SYS_TCSETPGRP:
            return sys_tcsetpgrp(arg1);

        case SYS_TCGETPGRP:
            return sys_tcgetpgrp();

        case SYS_RUN_JOB_CONTROL_DEMO:
            vga_write("\n");
            run_job_control_demo();
            return 0;

        /* ===== Filesystem metadata mutation syscalls (ramfs) ===== */
        case SYS_UNLINK:
            /* arg1 = parent directory path (const char *)
               arg2 = name to remove (const char *) */
            if (arg1 && arg2) {
                struct vnode *parent = NULL;
                if (vfs_resolve_path((const char *)arg1, &parent) != 0 || !parent)
                    return (uint64_t)-1;
                return (uint64_t)vfs_unlink(parent, (const char *)arg2);
            }
            return (uint64_t)-1;

        case SYS_RENAME:
            /* arg1 = old full path (const char *)
               arg2 = new full path (const char *)
               Splits each path into parent dir + basename. */
            if (arg1 && arg2) {
                const char *old_path = (const char *)arg1;
                const char *new_path = (const char *)arg2;

                /* Helper: find last '/' and split */
                char old_parent_buf[256], new_parent_buf[256];
                const char *old_name, *new_name;

                const char *old_slash = old_path;
                const char *p;
                for (p = old_path; *p; p++) if (*p == '/') old_slash = p;
                if (old_slash == old_path) {
                    old_parent_buf[0] = '/'; old_parent_buf[1] = '\0';
                    old_name = old_path + 1;
                } else {
                    int len = (int)(old_slash - old_path);
                    if (len >= 255) len = 255;
                    for (int i = 0; i < len; i++) old_parent_buf[i] = old_path[i];
                    old_parent_buf[len] = '\0';
                    old_name = old_slash + 1;
                }

                const char *new_slash = new_path;
                for (p = new_path; *p; p++) if (*p == '/') new_slash = p;
                if (new_slash == new_path) {
                    new_parent_buf[0] = '/'; new_parent_buf[1] = '\0';
                    new_name = new_path + 1;
                } else {
                    int len = (int)(new_slash - new_path);
                    if (len >= 255) len = 255;
                    for (int i = 0; i < len; i++) new_parent_buf[i] = new_path[i];
                    new_parent_buf[len] = '\0';
                    new_name = new_slash + 1;
                }

                struct vnode *old_parent = NULL, *new_parent = NULL;
                if (vfs_resolve_path(old_parent_buf, &old_parent) != 0 || !old_parent)
                    return (uint64_t)-1;
                if (vfs_resolve_path(new_parent_buf, &new_parent) != 0 || !new_parent)
                    return (uint64_t)-1;
                return (uint64_t)vfs_rename(old_parent, old_name, new_parent, new_name);
            }
            return (uint64_t)-1;

        case SYS_CHMOD:
            /* arg1 = path (const char *), arg2 = mode (uint16_t) */
            if (arg1) {
                struct vnode *vp = NULL;
                if (vfs_resolve_path((const char *)arg1, &vp) != 0 || !vp)
                    return (uint64_t)-1;
                return (uint64_t)vfs_chmod(vp, (uint16_t)arg2);
            }
            return (uint64_t)-1;

        /* ===== FAT32 filesystem syscalls ===== */
        case SYS_RUN_FAT32_DEMO:
            /* Run FAT32 filesystem demo from shell */
            vga_write("\n");
            run_fat32_demo();
            return 0;

        case SYS_FAT32_MOUNT:
            uart_puts("[SYSCALL] SYS_FAT32_MOUNT (50) reached\n");
            shell_fat32_mount((const char *)arg1);
            return 0;

        case SYS_FAT32_UMOUNT:
            shell_fat32_umount((const char *)arg1);
            return 0;

        case SYS_FAT32_LS:
            shell_fat32_ls((const char *)arg1);
            return 0;

        case SYS_FAT32_CAT:
            shell_fat32_cat((const char *)arg1);
            return 0;

        case SYS_FAT32_WRITE:
            shell_fat32_write((const char *)arg1);
            return 0;

        case SYS_FAT32_MKDIR:
            shell_fat32_mkdir((const char *)arg1);
            return 0;

        case SYS_FAT32_RM:
            shell_fat32_rm((const char *)arg1);
            return 0;

        case SYS_FAT32_CD:
            shell_fat32_cd((const char *)arg1);
            return 0;

        case SYS_FAT32_RENAME:
            shell_fat32_rename((const char *)arg1);
            return 0;

        case SYS_FAT32_CHMOD:
            shell_fat32_chmod((const char *)arg1);
            return 0;

        case SYS_FAT32_TOUCH:
            shell_fat32_touch((const char *)arg1);
            return 0;

        case 99:
            /* Exit from usermode password demo - return to kernel */
            __asm__ volatile(
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                ::: "ax"
            );
            
            if (usermode_pass_return_rip != 0 && !usermode_pass_completed) {
                /* Don't wait for ESC - return immediately */
                usermode_pass_completed = 1;
                __asm__ volatile("swapgs");
                __asm__ volatile(
                    "mov %0, %%rsp\n"
                    "mov %1, %%rbp\n"
                    "jmp *%2\n"
                    :
                    : "r"(usermode_pass_return_rsp), "r"(usermode_pass_return_rbp), "r"(usermode_pass_return_rip)
                );
            }
            return 0;

        default:
            uart_puts("[KERNEL] Unknown syscall: ");
            uart_putu(syscall_num);
            uart_puts("\n");
            return (uint64_t)-1;
    }
}