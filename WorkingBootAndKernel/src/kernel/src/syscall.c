#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/errno.h"
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
#include "include/tty.h"
#include "uart.h"
#include "include/socket.h"
#include "include/fd.h"
#include "include/uaccess.h"
#include "include/pipe.h"

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

/* Skip past the first word and spaces in a cmdline to get the argument.
 * e.g. "userdel admin2" → returns pointer to "admin2" */
static const char *skip_to_arg1(const char *cmdline) {
    if (!cmdline) return NULL;
    while (*cmdline && *cmdline != ' ') cmdline++;
    while (*cmdline == ' ') cmdline++;
    return *cmdline ? cmdline : NULL;
}

/* Case-insensitive character comparison */
static int to_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Check if the current user has FAT32 write permission.
 * - Not logged in → deny
 * - Admin (uid 0 or is_admin) → allow everywhere
 * - Regular user → only inside /home/<username>
 * - Guest (logged out) → deny */
static int fat32_check_write_perm(void) {
    if (shell_current_uid < 0) return 0;
    if (shell_current_uid == 0) return 1;
    if (password_store_is_admin(shell_current_uid)) return 1;
    /* Regular user: check if CWD is at or under /home/<username> */
    const char *cwd = shell_fat32_get_cwd_path();
    char uname[32];
    if (password_store_get_username(shell_current_uid, uname, 32) != 0 || !uname[0])
        return 0;
    /* Build expected prefix "/home/<username>" */
    char prefix[64];
    int pos = 0;
    const char *hp = "/home/";
    while (*hp) prefix[pos++] = *hp++;
    for (int i = 0; uname[i] && pos < 62; i++) prefix[pos++] = uname[i];
    prefix[pos] = '\0';
    /* Case-insensitive prefix match */
    for (int i = 0; i < pos; i++) {
        if (!cwd[i]) return 0;
        if (to_lower((unsigned char)cwd[i]) != to_lower((unsigned char)prefix[i]))
            return 0;
    }
    /* CWD must be exactly the prefix or have a '/' after it */
    if (cwd[pos] == '\0' || cwd[pos] == '/') return 1;
    return 0;
}

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
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
            return sys_exec((const char*)arg1, (char* const*)arg2);

        case SYS_WAIT:
            /* arg1 = status pointer */
            if (arg1 && validate_user_buf((void *)arg1, sizeof(int)))
                return (uint64_t)(int64_t)-EFAULT;
            return sys_wait((int*)arg1);

        case SYS_WAITPID:
            /* arg1 = pid, arg2 = status, arg3 = options */
            if (arg2 && validate_user_buf((void *)arg2, sizeof(int)))
                return (uint64_t)(int64_t)-EFAULT;
            return sys_waitpid((int)arg1, (int*)arg2, (int)arg3);

        case SYS_PASS_VERIFY:
            /* arg1 = username, arg2 = password */
            if (validate_user_string((const char *)arg1) || validate_user_string((const char *)arg2))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)password_store_verify((const char*)arg1, (const char*)arg2);

        case SYS_PASS_STORE:
            /* arg1 = username, arg2 = password */
            if (validate_user_string((const char *)arg1) || validate_user_string((const char *)arg2))
                return (uint64_t)(int64_t)-EFAULT;
            {
                int rc = password_store_add((const char*)arg1, (const char*)arg2);
                if (rc == 0) {
                    /* Create home directory for the new user */
                    shell_fat32_ensure_home((const char*)arg1);
                }
                return (uint64_t)rc;
            }

        case SYS_PASS_EXISTS:
            /* arg1 = username */
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)password_store_user_exists((const char*)arg1);

        case SYS_WRITE:
            /* arg1 = string pointer, arg2 = length */
            if (arg1 && arg2) {
                if (validate_user_buf((const void *)arg1, arg2))
                    return (uint64_t)(int64_t)-EFAULT;
                tty_write_n(tty_console(), (const char *)arg1, (size_t)arg2);
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
            tty_putc(tty_console(), (char)arg1);
            return 0;

        case SYS_GETUID: {
            /* Return UID from the current process PCB.
             * Sign-extend so UID -1 (not logged in) is negative in 64-bit. */
            pcb_t *_p = get_current_process();
            return _p ? (uint64_t)(int64_t)(int32_t)_p->uid : (uint64_t)(int64_t)-ESRCH;
        }

        case SYS_SETUID: {
            /* Root (UID 0) may set any UID.
             * Guest (UID -1 / not logged in) may set any UID (login).
             * Any user may set UID to -1 (logout).
             * Any user may keep their own UID (no-op). */
            pcb_t *_p = get_current_process();
            if (!_p) return (uint64_t)(int64_t)-ESRCH;
            if (_p->uid != 0 && _p->uid != (uint32_t)-1 &&
                (uint32_t)arg1 != (uint32_t)-1 && _p->uid != (uint32_t)arg1) {
                uart_puts("[SHELL] SYS_SETUID: permission denied\n");
                return (uint64_t)(int64_t)-EPERM;
            }
            int was_guest = (_p->uid == (uint32_t)-1);
            _p->uid = (uint32_t)arg1;
            shell_current_uid = (int)arg1;  /* keep legacy shell var in sync */
            /* Auto-cd to home dir on login */
            if (was_guest && shell_current_uid >= 0) {
                char _uname[32];
                if (password_store_get_username(shell_current_uid, _uname, 32) == 0 && _uname[0])
                    shell_fat32_cd_to_home(_uname);
            }
            /* On logout, cd back to root */
            if (shell_current_uid < 0) {
                shell_fat32_cd_to_home("guest");
            }
            uart_puts("[SHELL] UID set to ");
            uart_putu(_p->uid);
            uart_puts("\n");
            return 0;
        }

        case SYS_GET_USERNAME:
            /* arg1 = buffer, arg2 = buffer size */
            if (validate_user_buf((void *)arg1, arg2))
                return (uint64_t)(int64_t)-EFAULT;
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
            /* Clear the screen via TTY */
            tty_clear(tty_console());
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
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
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
                if (validate_user_string((const char *)arg1))
                    return (uint64_t)(int64_t)-EFAULT;
                int ret = shell_fs_dispatch((const char *)arg1);
                return (uint64_t)ret;
            }
            return (uint64_t)(int64_t)-EINVAL;

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
                if (validate_user_string((const char *)arg1) || validate_user_string((const char *)arg2))
                    return (uint64_t)(int64_t)-EFAULT;
                struct vnode *parent = NULL;
                if (vfs_resolve_path((const char *)arg1, &parent) != 0 || !parent)
                    return (uint64_t)(int64_t)-ENOENT;
                return (uint64_t)vfs_unlink(parent, (const char *)arg2);
            }
            return (uint64_t)(int64_t)-EINVAL;

        case SYS_RENAME:
            /* arg1 = old full path (const char *)
               arg2 = new full path (const char *)
               Splits each path into parent dir + basename. */
            if (arg1 && arg2) {
                if (validate_user_string((const char *)arg1) || validate_user_string((const char *)arg2))
                    return (uint64_t)(int64_t)-EFAULT;
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
                    return (uint64_t)(int64_t)-ENOENT;
                if (vfs_resolve_path(new_parent_buf, &new_parent) != 0 || !new_parent)
                    return (uint64_t)(int64_t)-ENOENT;
                return (uint64_t)vfs_rename(old_parent, old_name, new_parent, new_name);
            }
            return (uint64_t)(int64_t)-EINVAL;

        case SYS_CHMOD:
            /* arg1 = path (const char *), arg2 = mode (uint16_t) */
            if (arg1) {
                if (validate_user_string((const char *)arg1))
                    return (uint64_t)(int64_t)-EFAULT;
                struct vnode *vp = NULL;
                if (vfs_resolve_path((const char *)arg1, &vp) != 0 || !vp)
                    return (uint64_t)(int64_t)-ENOENT;
                return (uint64_t)vfs_chmod(vp, (uint16_t)arg2);
            }
            return (uint64_t)(int64_t)-EINVAL;

        /* ===== FAT32 filesystem syscalls ===== */
        case SYS_RUN_FAT32_DEMO:
            /* Run FAT32 filesystem demo from shell */
            vga_write("\n");
            run_fat32_demo();
            return 0;

        case SYS_FAT32_MOUNT:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            uart_puts("[SYSCALL] SYS_FAT32_MOUNT (50) reached\n");
            shell_fat32_mount((const char *)arg1);
            return 0;

        case SYS_FAT32_UMOUNT:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            shell_fat32_umount((const char *)arg1);
            return 0;

        case SYS_FAT32_LS:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            shell_fat32_ls((const char *)arg1);
            return 0;

        case SYS_FAT32_CAT:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            shell_fat32_cat((const char *)arg1);
            return 0;

        case SYS_FAT32_WRITE:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
            shell_fat32_write((const char *)arg1);
            return 0;

        case SYS_FAT32_MKDIR:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
            shell_fat32_mkdir((const char *)arg1);
            return 0;

        case SYS_FAT32_RM:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
            shell_fat32_rm((const char *)arg1);
            return 0;

        case SYS_FAT32_CD:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            shell_fat32_cd((const char *)arg1);
            return 0;

        case SYS_FAT32_RENAME:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
            shell_fat32_rename((const char *)arg1);
            return 0;

        case SYS_FAT32_CHMOD:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
            shell_fat32_chmod((const char *)arg1);
            return 0;

        case SYS_FAT32_TOUCH:
            if (validate_user_string((const char *)arg1)) return (uint64_t)(int64_t)-EFAULT;
            if (!fat32_check_write_perm()) { vga_write("[ERROR] Permission denied\n"); return (uint64_t)(int64_t)-EACCES; }
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

        case SYS_READLINE:
            /* Line editing with cursor, arrows, history — delegated to TTY */
            /* arg1 = buffer pointer,  arg2 = max length (including NUL) */
            if (validate_user_buf((void *)arg1, arg2))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)tty_readline(tty_console(), (char *)arg1, (int)arg2);

        case SYS_READLINE_MASKED:
            /* Same as SYS_READLINE but echoes '*' instead of actual chars */
            /* arg1 = buffer pointer,  arg2 = max length (including NUL) */
            if (validate_user_buf((void *)arg1, arg2))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)tty_readline_masked(tty_console(), (char *)arg1, (int)arg2, '*');

        /* ===== Socket syscalls ===== */

        case SYS_SOCKET:
            /* socket(domain, type, protocol)
             * arg1 = domain   (AF_INET)
             * arg2 = type     (SOCK_DGRAM / SOCK_STREAM)
             * arg3 = protocol (0 = auto, IPPROTO_UDP, IPPROTO_TCP)
             * returns: non-negative fd on success, negative SOCK_ERR_* on failure */
            return (uint64_t)(int64_t)sock_create((int)arg1, (int)arg2, (int)arg3);

        case SYS_BIND:
            /* bind(fd, *sockaddr_in)
             * arg1 = socket fd
             * arg2 = pointer to sockaddr_in_t
             * returns: 0 on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((const void *)arg2, sizeof(sockaddr_in_t)))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)(int64_t)sock_bind((int)arg1, (const sockaddr_in_t *)arg2);

        case SYS_SENDTO:
            /* sendto(fd, *socket_io_args_t)
             * arg1 = socket fd
             * arg2 = pointer to socket_io_args_t { buf, len, addr (destination) }
             * returns: bytes sent on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((const void *)arg2, sizeof(socket_io_args_t)))
                return (uint64_t)(int64_t)-EFAULT;
            {
                socket_io_args_t *io = (socket_io_args_t *)arg2;
                return (uint64_t)(int64_t)sock_sendto((int)arg1, io->buf, io->len, &io->addr);
            }

        case SYS_RECVFROM:
            /* recvfrom(fd, *socket_io_args_t)
             * arg1 = socket fd
             * arg2 = pointer to socket_io_args_t { buf, len (capacity), addr (filled on return) }
             * returns: bytes received on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((void *)arg2, sizeof(socket_io_args_t)))
                return (uint64_t)(int64_t)-EFAULT;
            {
                socket_io_args_t *io = (socket_io_args_t *)arg2;
                return (uint64_t)(int64_t)sock_recvfrom((int)arg1, io->buf, io->len, &io->addr);
            }

        case SYS_CONNECT:
            /* connect(fd, *sockaddr_in)
             * arg1 = socket fd
             * arg2 = pointer to sockaddr_in_t (remote address)
             * returns: 0 on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((const void *)arg2, sizeof(sockaddr_in_t)))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)(int64_t)sock_connect((int)arg1, (const sockaddr_in_t *)arg2);

        case SYS_SEND:
            /* send(fd, buf, len)  — requires prior connect()
             * arg1 = socket fd
             * arg2 = pointer to payload buffer
             * arg3 = payload length
             * returns: bytes sent on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((const void *)arg2, arg3))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)(int64_t)sock_send((int)arg1, (const void *)arg2, (uint16_t)arg3);

        case SYS_RECV:
            /* recv(fd, buf, maxlen)  — requires prior connect()
             * arg1 = socket fd
             * arg2 = pointer to receive buffer
             * arg3 = maximum bytes to receive
             * returns: bytes received on success, negative SOCK_ERR_* on failure */
            if (!arg2) return (uint64_t)(int64_t)SOCK_ERR_INVAL;
            if (validate_user_buf((void *)arg2, arg3))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)(int64_t)sock_recv((int)arg1, (void *)arg2, (uint16_t)arg3);

        case SYS_SOCK_CLOSE:
            /* sock_close(fd)
             * arg1 = socket fd
             * returns: 0 on success, negative SOCK_ERR_* on failure */
            return (uint64_t)(int64_t)sock_close((int)arg1);

        case SYS_SHUTDOWN:
            shutdown_system();
            return 0;  /* never reached */

        case SYS_PASS_CHANGE:
            /* arg1 = old_password, arg2 = new_password; acts on current logged-in user */
            if (shell_current_uid < 0) return (uint64_t)(int64_t)-EPERM;
            if (validate_user_string((const char *)arg1) || validate_user_string((const char *)arg2))
                return (uint64_t)(int64_t)-EFAULT;
            return (uint64_t)password_store_change_password(
                shell_current_uid, (const char*)arg1, (const char*)arg2);

        case SYS_PASS_DELETE: {
            /* arg1 = cmdline "userdel <username>"; only admins may do this */
            if (shell_current_uid < 0 || !password_store_is_admin(shell_current_uid)) {
                uart_puts("[KERNEL] userdel: permission denied\n");
                return (uint64_t)(int64_t)-EPERM;
            }
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
            const char *del_name = skip_to_arg1((const char*)arg1);
            if (!del_name) return (uint64_t)(int64_t)-EINVAL;
            int del_uid = password_store_get_uid(del_name);
            if (del_uid < 0) return (uint64_t)(int64_t)-ENOENT;
            return (uint64_t)password_store_delete(del_uid);
        }

        case SYS_PROMOTE_USER: {
            /* arg1 = cmdline "promote <username>"; only admins may do this */
            if (shell_current_uid < 0 || !password_store_is_admin(shell_current_uid)) {
                vga_write("[ERROR] Permission denied: admin only.\n");
                return (uint64_t)(int64_t)-EPERM;
            }
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
            const char *pname = skip_to_arg1((const char*)arg1);
            if (!pname) { vga_write("[ERROR] Usage: promote <username>\n"); return (uint64_t)(int64_t)-EINVAL; }
            int puid = password_store_get_uid(pname);
            if (puid < 0) { vga_write("[ERROR] User not found.\n"); return (uint64_t)(int64_t)-ENOENT; }
            if (puid == 0) { vga_write("[INFO] User is already root admin.\n"); return 0; }
            password_store_set_admin(puid, 1);
            vga_write("[OK] User promoted to admin.\n");
            return 0;
        }

        case SYS_DEMOTE_USER: {
            /* arg1 = cmdline "demote <username>"; only admins may do this */
            if (shell_current_uid < 0 || !password_store_is_admin(shell_current_uid)) {
                vga_write("[ERROR] Permission denied: admin only.\n");
                return (uint64_t)(int64_t)-EPERM;
            }
            if (validate_user_string((const char *)arg1))
                return (uint64_t)(int64_t)-EFAULT;
            const char *dname = skip_to_arg1((const char*)arg1);
            if (!dname) { vga_write("[ERROR] Usage: demote <username>\n"); return (uint64_t)(int64_t)-EINVAL; }
            int duid = password_store_get_uid(dname);
            if (duid < 0) { vga_write("[ERROR] User not found.\n"); return (uint64_t)(int64_t)-ENOENT; }
            if (duid == 0) { vga_write("[ERROR] Cannot demote root admin.\n"); return (uint64_t)(int64_t)-EPERM; }
            password_store_set_admin(duid, 0);
            vga_write("[OK] Admin privileges revoked.\n");
            return 0;
        }

        /* ===== File descriptor syscalls ===== */

        case SYS_OPEN: {
            /* arg1 = path (const char*), arg2 = flags */
            const char *path = (const char *)arg1;
            uint16_t flags = (uint16_t)arg2;
            if (!path) return (uint64_t)(int64_t)-EINVAL;
            if (validate_user_string(path))
                return (uint64_t)(int64_t)-EFAULT;

            struct vnode *vp = NULL;
            int r = vfs_resolve_path(path, &vp);
            if (r != 0 || !vp) return (uint64_t)(int64_t)-ENOENT;

            if (vp->v_op && vp->v_op->vop_open) {
                r = vp->v_op->vop_open(vp);
                if (r != 0) return (uint64_t)(int64_t)-EIO;
            }

            struct file *f = file_alloc(vp, flags);
            if (!f) return (uint64_t)(int64_t)-ENOMEM;

            pcb_t *cur = get_current_process();
            if (!cur) { f->refcount = 0; return (uint64_t)(int64_t)-ESRCH; }
            int fd = fd_alloc(cur, f);
            if (fd < 0) { f->refcount = 0; return (uint64_t)(int64_t)fd; }
            /* fd_alloc bumped refcount to 2; drop the initial 1 from file_alloc */
            f->refcount--;
            return (uint64_t)fd;
        }

        case SYS_READ: {
            /* arg1 = fd, arg2 = buf, arg3 = len */
            pcb_t *cur = get_current_process();
            if (!cur) return (uint64_t)(int64_t)-ESRCH;
            struct file *f = fd_get(cur, (int)arg1);
            if (!f) return (uint64_t)(int64_t)-EBADF;
            if ((f->f_flags & O_ACCMODE) == O_WRONLY) return (uint64_t)(int64_t)-EBADF;
            if (!f->f_vnode) return (uint64_t)(int64_t)-EBADF;
            if (validate_user_buf((void *)arg2, arg3))
                return (uint64_t)(int64_t)-EFAULT;

            if (f->f_vnode->v_op && f->f_vnode->v_op->vop_read_at) {
                int n = f->f_vnode->v_op->vop_read_at(f->f_vnode, (void *)arg2, (size_t)arg3, f->f_offset);
                if (n > 0) f->f_offset += n;
                return (uint64_t)(int64_t)n;
            }
            if (f->f_vnode->v_op && f->f_vnode->v_op->vop_read) {
                int n = f->f_vnode->v_op->vop_read(f->f_vnode, (void *)arg2, (size_t)arg3);
                if (n > 0) f->f_offset += n;
                return (uint64_t)(int64_t)n;
            }
            return (uint64_t)(int64_t)-ENOSYS;
        }

        case SYS_WRITE_FD: {
            /* arg1 = fd, arg2 = buf, arg3 = len */
            pcb_t *cur = get_current_process();
            if (!cur) return (uint64_t)(int64_t)-ESRCH;
            struct file *f = fd_get(cur, (int)arg1);
            if (!f) return (uint64_t)(int64_t)-EBADF;
            if ((f->f_flags & O_ACCMODE) == O_RDONLY) return (uint64_t)(int64_t)-EBADF;
            if (!f->f_vnode) return (uint64_t)(int64_t)-EBADF;
            if (validate_user_buf((const void *)arg2, arg3))
                return (uint64_t)(int64_t)-EFAULT;

            if (f->f_vnode->v_op && f->f_vnode->v_op->vop_write_at) {
                int n = f->f_vnode->v_op->vop_write_at(f->f_vnode, (const void *)arg2, (size_t)arg3, f->f_offset);
                if (n > 0) f->f_offset += n;
                return (uint64_t)(int64_t)n;
            }
            if (f->f_vnode->v_op && f->f_vnode->v_op->vop_write) {
                int n = f->f_vnode->v_op->vop_write(f->f_vnode, (void *)arg2, (size_t)arg3);
                if (n > 0) f->f_offset += n;
                return (uint64_t)(int64_t)n;
            }
            return (uint64_t)(int64_t)-ENOSYS;
        }

        case SYS_CLOSE: {
            pcb_t *cur = get_current_process();
            if (!cur) return (uint64_t)(int64_t)-ESRCH;
            return (uint64_t)(int64_t)fd_close(cur, (int)arg1);
        }

        case SYS_DUP: {
            pcb_t *cur = get_current_process();
            if (!cur) return (uint64_t)(int64_t)-ESRCH;
            return (uint64_t)(int64_t)fd_dup(cur, (int)arg1);
        }

        case SYS_PIPE: {
            /* arg1 = pointer to int[2] in user space */
            if (validate_user_buf((void *)arg1, 2 * sizeof(int)))
                return (uint64_t)(int64_t)-EFAULT;
            pcb_t *cur = get_current_process();
            if (!cur) return (uint64_t)(int64_t)-ESRCH;
            return (uint64_t)(int64_t)pipe_create(cur, (int *)arg1);
        }

        default:
            uart_puts("[KERNEL] Unknown syscall: ");
            uart_putu(syscall_num);
            uart_puts("\n");
            return (uint64_t)(int64_t)-ENOSYS;
    }
}